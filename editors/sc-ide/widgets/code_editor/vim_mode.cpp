#include "vim_mode.hpp"
#include "sc_editor.hpp"
#include "../main_window.hpp"

#include <QKeyEvent>
#include <QTextBlock>
#include <QTextDocument>
#include <QFontMetrics>

namespace ScIDE {

QString VimModeController::sRegister;
bool VimModeController::sRegisterLinewise = false;

VimModeController::VimModeController(QPlainTextEdit* editor): mEditor(editor) {}

QString VimModeController::modeName() const {
    switch (mMode) {
    case Mode::Normal: return QStringLiteral("NORMAL");
    case Mode::Insert: return QStringLiteral("INSERT");
    case Mode::Visual: return QStringLiteral("VISUAL");
    case Mode::VisualLine: return QStringLiteral("VISUAL LINE");
    }
    return QString();
}

void VimModeController::setEnabled(bool enabled) {
    mEnabled = enabled;
    resetPending();
    if (enabled)
        enterMode(Mode::Normal);
    else {
        QTextCursor cursor = mEditor->textCursor();
        cursor.clearSelection();
        mEditor->setTextCursor(cursor);
        mEditor->setCursorWidth(1);
        if (MainWindow::instance())
            MainWindow::instance()->updateVimStatus(QString(), false);
    }
}

void VimModeController::resetPending() {
    mPendingOperator = Operator::None;
    mPendingG = false;
}

void VimModeController::mouseRepositioned() {
    resetPending();
    if (mEnabled && (mMode == Mode::Visual || mMode == Mode::VisualLine))
        enterMode(Mode::Normal);
    else
        updateCursorAppearance();
}

bool VimModeController::handleKeyPress(QKeyEvent* event) {
    if (!mEnabled)
        return false;

    if (event->key() == Qt::Key_Escape) {
        if (mMode == Mode::Insert) {
            QTextCursor cursor = mEditor->textCursor();
            if (cursor.positionInBlock() > 0)
                cursor.movePosition(QTextCursor::PreviousCharacter);
            mEditor->setTextCursor(cursor);
        }
        enterMode(Mode::Normal);
        return true;
    }

    if (mMode == Mode::Insert)
        return false;

    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_R) {
        mEditor->redo();
        return true;
    }
    if (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::ShiftModifier)
        return false;

    QString key = event->text();
    if (key.isEmpty())
        return true;
    return mMode == Mode::Normal ? handleNormal(key) : handleVisual(key);
}

void VimModeController::enterMode(Mode mode) {
    mMode = mode;
    resetPending();
    if (mode == Mode::Visual || mode == Mode::VisualLine) {
        QTextCursor cursor = mEditor->textCursor();
        cursor.clearSelection();
        mVisualAnchor = cursor.position();
        updateVisualSelection();
    } else {
        mVisualAnchor = -1;
        QTextCursor cursor = mEditor->textCursor();
        cursor.clearSelection();
        mEditor->setTextCursor(cursor);
        if (mode == Mode::Normal)
            normalizeNormalCursor();
    }
    updateCursorAppearance();
    notifyMode();
}

void VimModeController::notifyMode() {
    if (MainWindow::instance() && mEditor->hasFocus())
        MainWindow::instance()->updateVimStatus(modeName(), mEnabled);
}

void VimModeController::updateCursorAppearance() {
    if (!mEnabled || mMode == Mode::Insert) {
        mEditor->setCursorWidth(1);
        return;
    }
    QTextCursor cursor = mEditor->textCursor();
    QChar ch = cursor.document()->characterAt(cursor.position());
    int width = mEditor->fontMetrics().horizontalAdvance(ch.isNull() || ch == QChar::ParagraphSeparator ? QChar('M') : ch);
    mEditor->setCursorWidth(qMax(2, width));
}

void VimModeController::normalizeNormalCursor() {
    QTextCursor cursor = mEditor->textCursor();
    cursor.clearSelection();
    if (cursor.atBlockEnd() && cursor.positionInBlock() > 0)
        cursor.movePosition(QTextCursor::PreviousCharacter);
    mEditor->setTextCursor(cursor);
}

void VimModeController::enterInsertAt(QTextCursor::MoveOperation operation) {
    QTextCursor cursor = mEditor->textCursor();
    cursor.clearSelection();
    if (operation != QTextCursor::NoMove)
        cursor.movePosition(operation);
    mEditor->setTextCursor(cursor);
    enterMode(Mode::Insert);
}

void VimModeController::openLine(bool above) {
    QTextCursor cursor = mEditor->textCursor();
    cursor.beginEditBlock();
    if (above) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.insertBlock();
        cursor.movePosition(QTextCursor::PreviousBlock);
    } else {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock();
    }
    cursor.endEditBlock();
    mEditor->setTextCursor(cursor);
    enterMode(Mode::Insert);
}

bool VimModeController::move(const QString& key, QTextCursor& cursor, QTextCursor::MoveMode mode) {
    QTextCursor::MoveOperation op = QTextCursor::NoMove;
    if (key == "h") op = QTextCursor::PreviousCharacter;
    else if (key == "l") op = QTextCursor::NextCharacter;
    else if (key == "j") op = QTextCursor::Down;
    else if (key == "k") op = QTextCursor::Up;
    else if (key == "w") op = QTextCursor::NextWord;
    else if (key == "b") op = QTextCursor::PreviousWord;
    else if (key == "e") op = QTextCursor::EndOfWord;
    else if (key == "0") op = QTextCursor::StartOfBlock;
    else if (key == "$") op = QTextCursor::EndOfBlock;
    else if (key == "G") op = QTextCursor::End;
    else if (key == "gg") op = QTextCursor::Start;
    else if (key == "^") {
        cursor.movePosition(QTextCursor::StartOfBlock, mode);
        while (!cursor.atBlockEnd() && cursor.document()->characterAt(cursor.position()).isSpace())
            cursor.movePosition(QTextCursor::NextCharacter, mode);
        return true;
    } else return false;
    cursor.movePosition(op, mode);
    return true;
}

bool VimModeController::handleNormal(const QString& key) {
    if (mPendingG) {
        mPendingG = false;
        if (key == "g") {
            QTextCursor cursor = mEditor->textCursor();
            move("gg", cursor, QTextCursor::MoveAnchor);
            mEditor->setTextCursor(cursor);
            normalizeNormalCursor();
        }
        return true;
    }
    if (mPendingOperator != Operator::None) {
        Operator op = mPendingOperator;
        mPendingOperator = Operator::None;
        const QString operatorKey = op == Operator::Delete ? "d" : op == Operator::Change ? "c" : "y";
        if (key == operatorKey)
            applyLineOperator(op);
        else
            applyOperator(op, key);
        return true;
    }

    if (key == "g") { mPendingG = true; return true; }
    if (key == "i") { enterInsertAt(); return true; }
    if (key == "I") { enterInsertAt(QTextCursor::StartOfBlock); return true; }
    if (key == "a") { enterInsertAt(QTextCursor::NextCharacter); return true; }
    if (key == "A") { enterInsertAt(QTextCursor::EndOfBlock); return true; }
    if (key == "o") { openLine(false); return true; }
    if (key == "O") { openLine(true); return true; }
    if (key == "v") { enterMode(Mode::Visual); return true; }
    if (key == "V") { enterMode(Mode::VisualLine); return true; }
    if (key == "x") { deleteCharacter(); return true; }
    if (key == "D") { applyOperator(Operator::Delete, "$"); return true; }
    if (key == "C") { applyOperator(Operator::Change, "$"); return true; }
    if (key == "Y") { applyLineOperator(Operator::Yank); return true; }
    if (key == "p") { paste(false); return true; }
    if (key == "P") { paste(true); return true; }
    if (key == "u") { mEditor->undo(); normalizeNormalCursor(); return true; }
    if (key == "d") { mPendingOperator = Operator::Delete; return true; }
    if (key == "c") { mPendingOperator = Operator::Change; return true; }
    if (key == "y") { mPendingOperator = Operator::Yank; return true; }

    QTextCursor cursor = mEditor->textCursor();
    if (move(key, cursor, QTextCursor::MoveAnchor)) {
        mEditor->setTextCursor(cursor);
        normalizeNormalCursor();
        updateCursorAppearance();
    }
    return true;
}

bool VimModeController::applyOperator(Operator op, const QString& motion) {
    QTextCursor cursor = mEditor->textCursor();
    int origin = cursor.position();
    if (!move(motion, cursor, QTextCursor::MoveAnchor))
        return false;
    int destination = cursor.position();
    if (destination == origin)
        return true;
    if (destination > origin && (motion == "e" || motion == "$"))
        ++destination;
    cursor.setPosition(origin);
    cursor.setPosition(qBound(0, destination, cursor.document()->characterCount() - 1), QTextCursor::KeepAnchor);
    sRegister = cursor.selectedText().replace(QChar::ParagraphSeparator, QChar('\n'));
    sRegisterLinewise = false;
    if (op != Operator::Yank) {
        cursor.beginEditBlock();
        cursor.removeSelectedText();
        cursor.endEditBlock();
        mEditor->setTextCursor(cursor);
    } else
        cursor.setPosition(origin);
    if (op == Operator::Change)
        enterMode(Mode::Insert);
    else {
        mEditor->setTextCursor(cursor);
        normalizeNormalCursor();
    }
    return true;
}

void VimModeController::applyLineOperator(Operator op) {
    QTextCursor cursor = mEditor->textCursor();
    cursor.movePosition(QTextCursor::StartOfBlock);
    int start = cursor.position();
    cursor.movePosition(QTextCursor::EndOfBlock);
    if (!cursor.atEnd())
        cursor.movePosition(QTextCursor::NextCharacter);
    cursor.setPosition(start, QTextCursor::KeepAnchor);
    sRegister = cursor.selectedText().replace(QChar::ParagraphSeparator, QChar('\n'));
    if (!sRegister.endsWith('\n'))
        sRegister.append('\n');
    sRegisterLinewise = true;
    if (op == Operator::Change) {
        cursor.clearSelection();
        cursor.setPosition(start);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.beginEditBlock();
        cursor.removeSelectedText();
        cursor.endEditBlock();
    } else if (op == Operator::Delete) {
        cursor.beginEditBlock();
        cursor.removeSelectedText();
        cursor.endEditBlock();
    }
    mEditor->setTextCursor(cursor);
    if (op == Operator::Change)
        enterMode(Mode::Insert);
    else
        normalizeNormalCursor();
}

void VimModeController::deleteCharacter() {
    QTextCursor cursor = mEditor->textCursor();
    if (cursor.atEnd() || cursor.atBlockEnd())
        return;
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    sRegister = cursor.selectedText();
    sRegisterLinewise = false;
    cursor.removeSelectedText();
    mEditor->setTextCursor(cursor);
    normalizeNormalCursor();
}

void VimModeController::paste(bool before) {
    if (sRegister.isEmpty())
        return;
    QTextCursor cursor = mEditor->textCursor();
    cursor.beginEditBlock();
    if (sRegisterLinewise) {
        if (before)
            cursor.movePosition(QTextCursor::StartOfBlock);
        else {
            cursor.movePosition(QTextCursor::EndOfBlock);
            if (!cursor.atEnd()) cursor.movePosition(QTextCursor::NextCharacter);
            else cursor.insertBlock();
        }
        cursor.insertText(sRegister);
    } else {
        if (!before && !cursor.atBlockEnd())
            cursor.movePosition(QTextCursor::NextCharacter);
        cursor.insertText(sRegister);
        cursor.movePosition(QTextCursor::PreviousCharacter);
    }
    cursor.endEditBlock();
    mEditor->setTextCursor(cursor);
    normalizeNormalCursor();
}

void VimModeController::updateVisualSelection() {
    QTextCursor cursor = mEditor->textCursor();
    int active = cursor.position();
    if (mMode == Mode::VisualLine) {
        QTextCursor anchorCursor(cursor.document());
        anchorCursor.setPosition(mVisualAnchor);
        anchorCursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock);
        if (!cursor.atEnd()) cursor.movePosition(QTextCursor::NextCharacter);
        cursor.setPosition(anchorCursor.position(), QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(mVisualAnchor);
        int end = active;
        if (end >= mVisualAnchor && end < cursor.document()->characterCount() - 1)
            ++end;
        cursor.setPosition(end, QTextCursor::KeepAnchor);
    }
    mEditor->setTextCursor(cursor);
}

bool VimModeController::handleVisual(const QString& key) {
    if (key == "v" && mMode == Mode::Visual) { enterMode(Mode::Normal); return true; }
    if (key == "V") {
        if (mMode == Mode::VisualLine) enterMode(Mode::Normal);
        else { mMode = Mode::VisualLine; updateVisualSelection(); notifyMode(); }
        return true;
    }
    if (key == "d" || key == "x" || key == "c" || key == "y" || key == "p") {
        Operator op = key == "y" ? Operator::Yank : key == "c" ? Operator::Change : Operator::Delete;
        if (key == "p") {
            applySelectionOperator(Operator::Delete);
            paste(true);
        } else applySelectionOperator(op);
        return true;
    }
    QTextCursor cursor = mEditor->textCursor();
    int active = cursor.position();
    if (cursor.hasSelection())
        active = cursor.position() == cursor.selectionStart() ? cursor.selectionStart() : cursor.selectionEnd() - 1;
    cursor.clearSelection();
    cursor.setPosition(active);
    QString motion = key;
    if (mPendingG) {
        mPendingG = false;
        motion = key == "g" ? "gg" : QString();
    } else if (key == "g") {
        mPendingG = true;
        return true;
    }
    if (move(motion, cursor, QTextCursor::MoveAnchor)) {
        mEditor->setTextCursor(cursor);
        updateVisualSelection();
    }
    return true;
}

void VimModeController::applySelectionOperator(Operator op) {
    QTextCursor cursor = mEditor->textCursor();
    sRegister = cursor.selectedText().replace(QChar::ParagraphSeparator, QChar('\n'));
    sRegisterLinewise = mMode == Mode::VisualLine;
    if (sRegisterLinewise && !sRegister.endsWith('\n')) sRegister.append('\n');
    if (op != Operator::Yank) {
        cursor.beginEditBlock();
        cursor.removeSelectedText();
        cursor.endEditBlock();
    } else cursor.setPosition(cursor.selectionStart());
    mEditor->setTextCursor(cursor);
    if (op == Operator::Change)
        enterMode(Mode::Insert);
    else
        enterMode(Mode::Normal);
}

} // namespace ScIDE
