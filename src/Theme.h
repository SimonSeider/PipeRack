#pragma once
#include <QColor>
#include <QFont>
#include <QFontDatabase>

namespace Theme
{

    inline QColor windowBg() { return QColor(0x0f, 0x11, 0x15); }
    inline QColor surface() { return QColor(0x17, 0x1a, 0x20); }
    inline QColor surfaceHi() { return QColor(0x1e, 0x22, 0x2a); }
    inline QColor field() { return QColor(0x12, 0x15, 0x1a); }
    inline QColor border() { return QColor(0x27, 0x2c, 0x35); }
    inline QColor borderHi() { return QColor(0x3a, 0x41, 0x4d); }

    inline QColor textBright() { return QColor(0xe8, 0xea, 0xef); }
    inline QColor textDim() { return QColor(0x98, 0xa0, 0xac); }
    inline QColor textFaint() { return QColor(0x63, 0x6b, 0x77); }

    inline QColor signal() { return QColor(0x3d, 0xcf, 0x8e); }
    inline QColor signalDim() { return QColor(0x1f, 0x6b, 0x4c); }
    inline QColor warn() { return QColor(0xef, 0xb3, 0x3d); }
    inline QColor clip() { return QColor(0xef, 0x5a, 0x52); }
    inline QColor accent() { return QColor(0x5b, 0x9d, 0xff); }

    inline QColor screenBg() { return QColor(0x0d, 0x10, 0x14); }
    inline QColor screenGrid() { return QColor(0x1d, 0x22, 0x2a); }

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

    // Small all-caps label, used sparingly for captions.
    inline QFont capsFont(int px)
    {
        QFont f = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        f.setPixelSize(px);
        f.setWeight(QFont::DemiBold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        f.setCapitalization(QFont::AllUppercase);
        return f;
    }

}
