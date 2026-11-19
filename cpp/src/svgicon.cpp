#include "svgicon.hpp"

#include <QColor>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double vectorAngle(double ux, double uy, double vx, double vy) {
    const double dot = ux * vx + uy * vy;
    const double lengths = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
    const double cosine = lengths == 0.0 ? 1.0 : std::clamp(dot / lengths, -1.0, 1.0);
    const double angle = std::acos(cosine);
    return ux * vy - uy * vx < 0.0 ? -angle : angle;
}

void addEllipticalArc(QPainterPath& path, const QPointF& start, double rx, double ry,
                      double rotationDegrees, bool largeArc, bool sweep, const QPointF& end) {
    if (std::abs(start.x() - end.x()) < 1e-9 && std::abs(start.y() - end.y()) < 1e-9)
        return;
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx < 1e-9 || ry < 1e-9) {
        path.lineTo(end);
        return;
    }

    const double phi = rotationDegrees * kPi / 180.0;
    const double cosPhi = std::cos(phi);
    const double sinPhi = std::sin(phi);
    const double dx = (start.x() - end.x()) / 2.0;
    const double dy = (start.y() - end.y()) / 2.0;
    const double x1p = cosPhi * dx + sinPhi * dy;
    const double y1p = -sinPhi * dx + cosPhi * dy;

    double rxsq = rx * rx;
    double rysq = ry * ry;
    const double lambda = (x1p * x1p) / rxsq + (y1p * y1p) / rysq;
    if (lambda > 1.0) {
        const double grown = std::sqrt(lambda);
        rx *= grown;
        ry *= grown;
        rxsq = rx * rx;
        rysq = ry * ry;
    }

    const double numerator = rxsq * rysq - rxsq * y1p * y1p - rysq * x1p * x1p;
    const double denominator = rxsq * y1p * y1p + rysq * x1p * x1p;
    double centerScale = denominator == 0.0 ? 0.0 : numerator / denominator;
    if (centerScale < 0.0)
        centerScale = 0.0;
    centerScale = std::sqrt(centerScale);
    if (largeArc == sweep)
        centerScale = -centerScale;

    const double cxp = centerScale * rx * y1p / ry;
    const double cyp = centerScale * -ry * x1p / rx;
    const double cx = cosPhi * cxp - sinPhi * cyp + (start.x() + end.x()) / 2.0;
    const double cy = sinPhi * cxp + cosPhi * cyp + (start.y() + end.y()) / 2.0;
    const double theta1 = vectorAngle(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    double sweepAngle = vectorAngle((x1p - cxp) / rx, (y1p - cyp) / ry,
                                     (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && sweepAngle > 0.0)
        sweepAngle -= 2.0 * kPi;
    else if (sweep && sweepAngle < 0.0)
        sweepAngle += 2.0 * kPi;

    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(sweepAngle) / (kPi / 24.0))));
    for (int step = 1; step <= steps; ++step) {
        const double theta = theta1 + sweepAngle * (double(step) / double(steps));
        const double cosTheta = std::cos(theta);
        const double sinTheta = std::sin(theta);
        path.lineTo(cosPhi * rx * cosTheta - sinPhi * ry * sinTheta + cx,
                    sinPhi * rx * cosTheta + cosPhi * ry * sinTheta + cy);
    }
}

bool readNumber(const QString& text, int& index, double& value) {
    const int length = text.size();
    while (index < length && (text.at(index).isSpace() || text.at(index) == QLatin1Char(',')))
        ++index;
    if (index >= length || text.at(index).isLetter())
        return false;

    const int start = index;
    if (text.at(index) == QLatin1Char('+') || text.at(index) == QLatin1Char('-'))
        ++index;
    bool digit = false;
    bool dotted = false;
    bool exponent = false;
    while (index < length) {
        const QChar character = text.at(index);
        if (character.isDigit()) {
            digit = true;
            ++index;
        } else if (character == QLatin1Char('.') && !dotted && !exponent) {
            dotted = true;
            ++index;
        } else if ((character == QLatin1Char('e') || character == QLatin1Char('E')) && digit && !exponent) {
            exponent = true;
            ++index;
            if (index < length && (text.at(index) == QLatin1Char('+') || text.at(index) == QLatin1Char('-')))
                ++index;
        } else {
            break;
        }
    }
    if (!digit)
        return false;
    value = text.mid(start, index - start).toDouble();
    return true;
}

QPainterPath pathFromData(const QString& data) {
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    QPointF current;
    QPointF subpathStart;
    bool drawing = false;
    char command = 0;
    int index = 0;
    const auto number = [&data, &index](double& value) { return readNumber(data, index, value); };

    while (index < data.size()) {
        while (index < data.size() && (data.at(index).isSpace() || data.at(index) == QLatin1Char(',')))
            ++index;
        if (index >= data.size())
            break;
        if (data.at(index).isLetter()) {
            command = data.at(index).toLatin1();
            ++index;
        } else if (command == 'M') {
            command = 'L';
        } else if (command == 'm') {
            command = 'l';
        } else if (command == 0) {
            break;
        }

        double x = 0;
        double y = 0;
        double x1 = 0;
        double y1 = 0;
        double x2 = 0;
        double y2 = 0;
        switch (command) {
        case 'M':
        case 'm':
            if (!number(x) || !number(y))
                return path;
            if (command == 'm' && drawing) {
                x += current.x();
                y += current.y();
            }
            current = QPointF(x, y);
            subpathStart = current;
            path.moveTo(current);
            drawing = true;
            break;
        case 'L':
        case 'l':
            if (!number(x) || !number(y))
                return path;
            if (command == 'l') {
                x += current.x();
                y += current.y();
            }
            current = QPointF(x, y);
            path.lineTo(current);
            drawing = true;
            break;
        case 'H':
        case 'h':
            if (!number(x))
                return path;
            if (command == 'h')
                x += current.x();
            current.setX(x);
            path.lineTo(current);
            drawing = true;
            break;
        case 'V':
        case 'v':
            if (!number(y))
                return path;
            if (command == 'v')
                y += current.y();
            current.setY(y);
            path.lineTo(current);
            drawing = true;
            break;
        case 'C':
        case 'c':
            if (!number(x1) || !number(y1) || !number(x2) || !number(y2) || !number(x) || !number(y))
                return path;
            if (command == 'c') {
                x1 += current.x();
                y1 += current.y();
                x2 += current.x();
                y2 += current.y();
                x += current.x();
                y += current.y();
            }
            path.cubicTo(QPointF(x1, y1), QPointF(x2, y2), QPointF(x, y));
            current = QPointF(x, y);
            drawing = true;
            break;
        case 'A':
        case 'a': {
            double rx = 0;
            double ry = 0;
            double rotation = 0;
            double large = 0;
            double sweep = 0;
            if (!number(rx) || !number(ry) || !number(rotation) || !number(large) || !number(sweep)
                || !number(x) || !number(y))
                return path;
            if (command == 'a') {
                x += current.x();
                y += current.y();
            }
            const QPointF end(x, y);
            addEllipticalArc(path, current, rx, ry, rotation, large != 0.0, sweep != 0.0, end);
            current = end;
            drawing = true;
            break;
        }
        case 'Z':
        case 'z':
            path.closeSubpath();
            current = subpathStart;
            break;
        default:
            return path;
        }
    }
    return path;
}

struct ViewBox {
    double x = 0;
    double y = 0;
    double width = 16;
    double height = 16;
};

ViewBox readViewBox(const QString& text) {
    ViewBox box;
    const QStringList parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() == 4) {
        box.x = parts.at(0).toDouble();
        box.y = parts.at(1).toDouble();
        box.width = parts.at(2).toDouble();
        box.height = parts.at(3).toDouble();
    }
    if (box.width <= 0.0)
        box.width = 16;
    if (box.height <= 0.0)
        box.height = 16;
    return box;
}

bool attributeIsNone(const QStringView& value) {
    return value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0;
}

QColor svgColor(QStringView text) {
    QString value = text.toString().trimmed();
    if (value.size() == 4 && value.at(0) == QLatin1Char('#')) {
        value = QStringLiteral("#%1%1%2%2%3%3").arg(value.at(1)).arg(value.at(2)).arg(value.at(3));
    }
    const QColor color(value);
    return color.isValid() ? color : QColor(Qt::black);
}

QImage renderSvg(const QByteArray& source, int size) {
    QXmlStreamReader xml(source);
    ViewBox viewBox;
    struct Element {
        QPainterPath path;
        bool fill = false;
        bool stroke = false;
        QColor fillColor = Qt::black;
        QColor strokeColor = Qt::black;
        double strokeWidth = 1;
        bool roundCap = false;
    };
    QList<Element> elements;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        const QStringView name = xml.name();
        if (name == QLatin1String("svg")) {
            const auto view = xml.attributes().value(QLatin1String("viewBox"));
            if (!view.isEmpty())
                viewBox = readViewBox(view.toString());
            continue;
        }
        if (name != QLatin1String("path") && name != QLatin1String("rect") && name != QLatin1String("circle"))
            continue;

        const auto attributes = xml.attributes();
        Element element;
        const auto fill = attributes.value(QLatin1String("fill"));
        const auto stroke = attributes.value(QLatin1String("stroke"));
        element.fill = fill.isEmpty() || !attributeIsNone(fill);
        element.stroke = !stroke.isEmpty() && !attributeIsNone(stroke);
        if (element.fill && !fill.isEmpty())
            element.fillColor = svgColor(fill);
        if (element.stroke && !stroke.isEmpty())
            element.strokeColor = svgColor(stroke);
        const auto width = attributes.value(QLatin1String("stroke-width"));
        if (!width.isEmpty())
            element.strokeWidth = width.toDouble();
        element.roundCap = attributes.value(QLatin1String("stroke-linecap"))
                           == QLatin1String("round");
        if (name == QLatin1String("path")) {
            element.path = pathFromData(attributes.value(QLatin1String("d")).toString());
        } else if (name == QLatin1String("circle")) {
            const double cx = attributes.value(QLatin1String("cx")).toDouble();
            const double cy = attributes.value(QLatin1String("cy")).toDouble();
            const double radius = attributes.value(QLatin1String("r")).toDouble();
            element.path.addEllipse(QPointF(cx, cy), radius, radius);
            element.path.setFillRule(Qt::WindingFill);
        } else {
            const double x = attributes.value(QLatin1String("x")).toDouble();
            const double y = attributes.value(QLatin1String("y")).toDouble();
            const double rectWidth = attributes.value(QLatin1String("width")).toDouble();
            const double rectHeight = attributes.value(QLatin1String("height")).toDouble();
            double rx = attributes.value(QLatin1String("rx")).toDouble();
            double ry = attributes.value(QLatin1String("ry")).toDouble();
            if (rx <= 0.0)
                rx = ry;
            if (ry <= 0.0)
                ry = rx;
            if (rx > 0.0 || ry > 0.0)
                element.path.addRoundedRect(QRectF(x, y, rectWidth, rectHeight), rx, ry);
            else
                element.path.addRect(QRectF(x, y, rectWidth, rectHeight));
            element.path.setFillRule(Qt::WindingFill);
        }
        elements.append(element);
    }
    if (xml.hasError() || elements.isEmpty())
        return {};

    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(double(size) / viewBox.width, double(size) / viewBox.height);
    painter.translate(-viewBox.x, -viewBox.y);
    for (const Element& element : elements) {
        if (element.fill) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(element.fillColor);
            painter.drawPath(element.path);
        }
        if (element.stroke) {
            QPen pen(element.strokeColor, element.strokeWidth);
            pen.setCosmetic(false);
            pen.setCapStyle(element.roundCap ? Qt::RoundCap : Qt::FlatCap);
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(element.path);
        }
    }
    painter.end();
    return image;
}

}  // namespace

QIcon svgIcon(const QString& name) {
    static QHash<QString, QIcon> cache;
    const auto found = cache.constFind(name);
    if (found != cache.constEnd())
        return found.value();

    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    QIcon icon;
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray source = file.readAll();
        for (const int size : {16, 32}) {
            const QImage image = renderSvg(source, size);
            if (!image.isNull())
                icon.addPixmap(QPixmap::fromImage(image));
        }
    }
    cache.insert(name, icon);
    return icon;
}
