#pragma once
#include <QColor>
#include <QFont>
#include <QFontDatabase>

namespace Theme
{

    inline QColor windowBg() { return QColor(0x14, 0x16, 0x19); }
    inline QColor railDark() { return QColor(0x1b, 0x1d, 0x21); }
    inline QColor railLight() { return QColor(0x2a, 0x2d, 0x33); }
    inline QColor holeDark() { return QColor(0x0a, 0x0b, 0x0d); }

    inline QColor plateTop() { return QColor(0x41, 0x46, 0x4e); }
    inline QColor plateBottom() { return QColor(0x2c, 0x30, 0x36); }
    inline QColor plateEdge() { return QColor(0x55, 0x5b, 0x64); }
    inline QColor plateShadow() { return QColor(0x0e, 0x0f, 0x12); }

    inline QColor textBright() { return QColor(0xe6, 0xe9, 0xee); }
    inline QColor textDim() { return QColor(0x96, 0x9d, 0xa8); }
    inline QColor textFaint() { return QColor(0x6b, 0x72, 0x7d); }

    inline QColor signal() { return QColor(0x46, 0xe0, 0x92); }
    inline QColor signalDim() { return QColor(0x24, 0x7a, 0x53); }
    inline QColor warn() { return QColor(0xf5, 0xb2, 0x3d); }
    inline QColor clip() { return QColor(0xf3, 0x5b, 0x51); }
    inline QColor accent() { return QColor(0x62, 0xb8, 0xf0); }

    inline QColor screenBg() { return QColor(0x07, 0x0b, 0x09); }
    inline QColor screenGrid() { return QColor(0x1a, 0x2a, 0x22); }

    inline QFont labelFont(int px, bool bold = false)
    {
        QFont f = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        f.setPixelSize(px);
        f.setBold(bold);
        return f;
    }

    inline QFont monoFont(int px, bool bold = false)
    {
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setPixelSize(px);
        f.setBold(bold);
        return f;
    }

    inline QFont engravedFont(int px)
    {
        QFont f = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        f.setPixelSize(px);
        f.setBold(true);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
        f.setCapitalization(QFont::AllUppercase);
        return f;
    }

}
