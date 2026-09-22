// flowkeyd 图标生成工具（一次性工具，见 AGENTS.md 第 10 节）。
//
// 把仓库根目录的 logo.svg 光栅化成两样产物（都在 assets/ 下，都要提交）：
//
//   * assets/flowkeyd.ico              —— Windows exe 的图标资源，由 windres 嵌进
//                                         exe（见 CMakeLists.txt 与
//                                         assets/flowkeyd.rc.in）
//   * assets/icons/flowkeyd-<n>.png    —— 运行时 QIcon 用的各个尺寸，编在 exe 的
//                                         qrc 里（见 src/app/app_icon.cpp）
//
// **正常构建（debug / release 两条 profile）都不碰它**：CMake 里它是
// `EXCLUDE_FROM_ALL`，所以只有改了 logo.svg 之后才需要手工跑一次
//
//     cmake --build --preset debug --target icons
//
// 为什么要有这个工具：exe 的图标只能是 .ico，而把 SVG 光栅化需要 Qt6::Svg；
// 运行时反而只用 PNG 帧，于是**发布的 exe 不需要 Qt6Svg.dll**，也不依赖
// imageformats/qsvg 插件（那两个都靠 windeployqt 猜，少一个就静默变成空白图标）。
//
// 用法：flowkeyd_icon_gen <logo.svg> <out-dir>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QIODevice>
#include <QPainter>
#include <QString>
#include <QSvgRenderer>

#include <array>
#include <cstdio>
#include <vector>

namespace {

/// 生成哪些尺寸：16/20/24/32/40/48 覆盖 100%–300% 缩放下的托盘图标，
/// 64/128/256 是资源管理器与 Alt+Tab 那边会用到的。
constexpr std::array<int, 9> kSizes{16, 20, 24, 32, 40, 48, 64, 128, 256};

/// 超过这个尺寸的帧在 .ico 里用 PNG 压缩（256x256 的 32 位位图光栅要 256 KB）。
/// 小尺寸用 DIB（BMP）帧：那是所有 Windows 版本都认的写法。
constexpr int kLargestDibEdge = 64;

/// ICO 目录项的一次性内存表示；偏移在写文件头时才算。
struct IconFrame
{
    int size = 0;
    QByteArray data;
};

void appendLe16(QByteArray &out, quint16 value)
{
    out.append(static_cast<char>(value & 0xff));
    out.append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray &out, quint32 value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        out.append(static_cast<char>((value >> shift) & 0xff));
    }
}

/// 画一张 `size`x`size` 的透明底图，再让 QSvgRenderer 按纵横比铺满它。
QImage renderFrame(QSvgRenderer &renderer, int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // logo.svg 的 viewBox 是正方形，所以这里不会变形；用默认的 KeepAspectRatio
    // 是为了将来换成非正方形画布时也不会被拉伸。
    renderer.render(&painter);
    return image;
}

QByteArray encodePng(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        return QByteArray();
    }
    return bytes;
}

/// DIB（BITMAPINFOHEADER + XOR 位图 + AND 掩码）帧。
/// AND 掩码必须存在（哪怕 32 位色的透明度走 alpha 通道），而且是自下而上的。
QByteArray encodeDib(const QImage &image)
{
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    const int width = source.width();
    const int height = source.height();
    const int maskStride = ((width + 31) / 32) * 4; // 1 bpp，每行按 4 字节对齐

    QByteArray out;
    appendLe32(out, 40);                    // biSize
    appendLe32(out, static_cast<quint32>(width));
    appendLe32(out, static_cast<quint32>(height * 2)); // XOR + AND
    appendLe16(out, 1);                     // biPlanes
    appendLe16(out, 32);                    // biBitCount
    appendLe32(out, 0);                     // biCompression = BI_RGB
    appendLe32(out, static_cast<quint32>(width * height * 4)); // biSizeImage
    appendLe32(out, 0);                     // biXPelsPerMeter
    appendLe32(out, 0);                     // biYPelsPerMeter
    appendLe32(out, 0);                     // biClrUsed
    appendLe32(out, 0);                     // biClrImportant

    // XOR 位图：自下而上。小端机器上 ARGB32 的内存字节顺序正好是 B,G,R,A。
    for (int y = height - 1; y >= 0; --y) {
        out.append(reinterpret_cast<const char *>(source.constScanLine(y)),
                   static_cast<qsizetype>(width) * 4);
    }
    out.append(QByteArray(maskStride * height, '\0'));
    return out;
}

/// 把帧打包成 .ico（ICONDIR + 每帧 16 字节目录项 + 帧数据）。
QByteArray encodeIco(const std::vector<IconFrame> &frames)
{
    QByteArray out;
    appendLe16(out, 0); // reserved
    appendLe16(out, 1); // type = icon
    appendLe16(out, static_cast<quint16>(frames.size()));

    quint32 offset = static_cast<quint32>(6 + frames.size() * 16);
    for (const IconFrame &frame : frames) {
        // 宽高是单字节：256 在这里写 0。
        out.append(static_cast<char>(frame.size >= 256 ? 0 : frame.size));
        out.append(static_cast<char>(frame.size >= 256 ? 0 : frame.size));
        out.append('\0'); // 调色板颜色数（32 位色写 0）
        out.append('\0'); // reserved
        appendLe16(out, 1);  // planes
        appendLe16(out, 32); // bitCount
        appendLe32(out, static_cast<quint32>(frame.data.size()));
        appendLe32(out, offset);
        offset += static_cast<quint32>(frame.data.size());
    }
    for (const IconFrame &frame : frames) {
        out.append(frame.data);
    }
    return out;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "error: cannot write %s\n", qPrintable(path));
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        std::fprintf(stderr, "error: short write on %s\n", qPrintable(path));
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <logo.svg> <out-dir>\n", argv[0]);
        return 2;
    }

    const QString svgPath = QString::fromLocal8Bit(argv[1]);
    const QDir outDir(QString::fromLocal8Bit(argv[2]));
    if (!outDir.exists()) {
        std::fprintf(stderr, "error: %s is not a directory\n", qPrintable(outDir.absolutePath()));
        return 1;
    }

    QSvgRenderer renderer(svgPath);
    if (!renderer.isValid()) {
        std::fprintf(stderr, "error: cannot render the SVG %s\n", qPrintable(svgPath));
        return 1;
    }

    const QDir iconDir(outDir.filePath(QStringLiteral("icons")));
    if (!iconDir.mkpath(QStringLiteral("."))) {
        std::fprintf(stderr, "error: cannot create %s\n", qPrintable(iconDir.absolutePath()));
        return 1;
    }

    std::vector<IconFrame> frames;
    frames.reserve(kSizes.size());
    for (const int size : kSizes) {
        const QImage image = renderFrame(renderer, size);
        const QByteArray png = encodePng(image);
        if (png.isEmpty()) {
            std::fprintf(stderr, "error: cannot encode the %dx%d PNG frame\n", size, size);
            return 1;
        }
        const QString pngPath =
            iconDir.filePath(QStringLiteral("flowkeyd-%1.png").arg(size));
        if (!writeFile(pngPath, png)) {
            return 1;
        }

        IconFrame frame;
        frame.size = size;
        frame.data = size <= kLargestDibEdge ? encodeDib(image) : png;
        frames.push_back(std::move(frame));
        std::printf("wrote %s (%lld bytes)\n", qPrintable(pngPath),
                    static_cast<long long>(png.size()));
    }

    const QByteArray ico = encodeIco(frames);
    const QString icoPath = outDir.filePath(QStringLiteral("flowkeyd.ico"));
    if (!writeFile(icoPath, ico)) {
        return 1;
    }
    std::printf("wrote %s (%lld bytes, %lld frame(s))\n", qPrintable(icoPath),
                static_cast<long long>(ico.size()),
                static_cast<long long>(frames.size()));
    return 0;
}
