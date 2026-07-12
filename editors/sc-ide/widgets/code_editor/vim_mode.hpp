#pragma once

#include <QTextCursor>
#include <QString>
#include <QVector>

class QKeyEvent;
class QPlainTextEdit;

namespace ScIDE {

class VimModeController {
public:
    enum class Mode { Normal, Insert, Visual, VisualLine };

    explicit VimModeController(QPlainTextEdit* editor);
    ~VimModeController();

    bool handleKeyPress(QKeyEvent* event);
    bool shouldOverrideShortcut(const QKeyEvent* event) const;
    void setEnabled(bool enabled);
    bool enabled() const { return mEnabled; }
    Mode mode() const { return mMode; }
    QString modeName() const;
    void resetPending();
    void mouseRepositioned();

private:
    struct InsertAction {
        int key;
        int modifiers;
        QString text;
    };

    enum class Operator { None, Delete, Change, Yank };
    enum class CharCommand { None, FindForward, FindBackward, TillForward, TillBackward, Replace };
    enum class LineCommand { None, Indent, Unindent, Reindent };

    bool handleNormal(const QString& key);
    bool handleVisual(const QString& key);
    bool handleSearchKey(QKeyEvent* event);
    bool move(const QString& key, QTextCursor& cursor, QTextCursor::MoveMode selectionMode, int count = 1);
    bool moveCharacter(QTextCursor& cursor, QChar target, CharCommand command, int count);
    bool applyOperator(Operator op, const QString& motion, int count);
    void applyRangeOperator(Operator op, int origin, int destination, bool inclusive);
    void applyLineOperator(Operator op, int count = 1);
    void applyLineOperatorTo(Operator op, int targetBlock);
    void applySelectionOperator(Operator op);
    bool applyTextObject(Operator op, bool inner, QChar object, int count, bool visual);
    bool selectTextObject(QTextCursor& cursor, bool inner, QChar object, int count);
    void enterMode(Mode mode);
    void enterInsertAt(QTextCursor::MoveOperation operation = QTextCursor::NoMove);
    void openLine(bool above, int count = 1);
    void paste(bool before, int count = 1);
    void deleteCharacter(int count = 1, bool backwards = false);
    void replaceCharacters(QChar value, int count);
    void toggleCase(int count);
    void joinLines(int count);
    void indentLines(LineCommand command, int count);
    void normalizeNormalCursor();
    void updateVisualSelection();
    void updateCursorAppearance();
    void notifyMode();
    void notifyStatus(const QString& text);

    void beginSearch(bool forward);
    bool updateSearchPreview();
    bool performSearch(const QString& pattern, bool forward, int origin, bool includeOrigin = false);
    void finishSearch(bool commit);
    bool searchWord(bool forward);

    int takeCount();
    bool commandPending() const;
    void beginCommandRecording(const QString& key);
    void finishCommandRecording();
    void recordInsertKey(QKeyEvent* event);
    void repeatLastChange(int count);
    void beginChangeEditBlock();
    void endChangeEditBlock();

    QPlainTextEdit* mEditor;
    Mode mMode { Mode::Normal };
    Operator mPendingOperator { Operator::None };
    CharCommand mPendingChar { CharCommand::None };
    LineCommand mPendingLine { LineCommand::None };
    bool mPendingG { false };
    bool mPendingTextObject { false };
    bool mTextObjectInner { false };
    bool mEnabled { false };
    int mVisualAnchor { -1 };
    int mCount { 0 };
    int mOperatorCount { 1 };

    bool mSearching { false };
    bool mSearchForward { true };
    int mSearchOrigin { 0 };
    QString mSearchInput;

    CharCommand mLastCharCommand { CharCommand::None };
    QChar mLastFindCharacter;

    bool mRecordingCommand { false };
    bool mRecordingInsert { false };
    bool mReplaying { false };
    bool mChangeEditBlockOpen { false };
    int mCommandRevision { 0 };
    QString mCommandKeys;
    QVector<InsertAction> mCommandInsertActions;
    QString mLastChangeKeys;
    QVector<InsertAction> mLastChangeInsertActions;

    static QString sRegister;
    static bool sRegisterLinewise;
    static QString sSearchPattern;
    static bool sSearchForward;
};

} // namespace ScIDE
