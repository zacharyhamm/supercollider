#pragma once

#include <QTextCursor>
#include <QString>

class QKeyEvent;
class QPlainTextEdit;

namespace ScIDE {

class VimModeController {
public:
    enum class Mode { Normal, Insert, Visual, VisualLine };

    explicit VimModeController(QPlainTextEdit* editor);

    bool handleKeyPress(QKeyEvent* event);
    void setEnabled(bool enabled);
    bool enabled() const { return mEnabled; }
    Mode mode() const { return mMode; }
    QString modeName() const;
    void resetPending();
    void mouseRepositioned();

private:
    enum class Operator { None, Delete, Change, Yank };

    bool handleNormal(const QString& key);
    bool handleVisual(const QString& key);
    bool move(const QString& key, QTextCursor& cursor, QTextCursor::MoveMode selectionMode);
    bool applyOperator(Operator op, const QString& motion);
    void applyLineOperator(Operator op);
    void applySelectionOperator(Operator op);
    void enterMode(Mode mode);
    void enterInsertAt(QTextCursor::MoveOperation operation = QTextCursor::NoMove);
    void openLine(bool above);
    void paste(bool before);
    void deleteCharacter();
    void normalizeNormalCursor();
    void updateVisualSelection();
    void updateCursorAppearance();
    void notifyMode();

    QPlainTextEdit* mEditor;
    Mode mMode { Mode::Normal };
    Operator mPendingOperator { Operator::None };
    bool mPendingG { false };
    bool mEnabled { false };
    int mVisualAnchor { -1 };

    static QString sRegister;
    static bool sRegisterLinewise;
};

} // namespace ScIDE
