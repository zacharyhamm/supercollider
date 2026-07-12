#include "vim_mode.hpp"
#include "sc_editor.hpp"
#include "../main_window.hpp"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QList>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>

namespace ScIDE {

QString VimModeController::sRegister;
bool VimModeController::sRegisterLinewise = false;
QString VimModeController::sSearchPattern;
bool VimModeController::sSearchForward = true;

namespace {

bool isWordCharacter(QChar ch) { return ch.isLetterOrNumber() || ch == QLatin1Char('_'); }

bool isEscaped(const QString& text, int position) {
    int slashes = 0;
    while (position > 0 && text.at(--position) == QLatin1Char('\\'))
        ++slashes;
    return slashes % 2 != 0;
}

} // namespace

VimModeController::VimModeController(QPlainTextEdit* editor): mEditor(editor) {}

VimModeController::~VimModeController() { endChangeEditBlock(); }

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
    if (mEnabled == enabled)
        return;
    endChangeEditBlock();
    mEnabled = enabled;
    resetPending();
    mSearching = false;
    mRecordingCommand = false;
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
    mPendingChar = CharCommand::None;
    mPendingLine = LineCommand::None;
    mPendingG = false;
    mPendingTextObject = false;
    mTextObjectInner = false;
    mCount = 0;
    mOperatorCount = 1;
}

bool VimModeController::commandPending() const {
    return mPendingOperator != Operator::None || mPendingChar != CharCommand::None || mPendingLine != LineCommand::None
        || mPendingG || mPendingTextObject || mCount > 0;
}

void VimModeController::mouseRepositioned() {
    resetPending();
    if (mSearching)
        finishSearch(false);
    if (mEnabled && (mMode == Mode::Visual || mMode == Mode::VisualLine))
        enterMode(Mode::Normal);
    else
        updateCursorAppearance();
}

bool VimModeController::handleKeyPress(QKeyEvent* event) {
    if (!mEnabled)
        return false;

    if (mSearching)
        return handleSearchKey(event);

    if (event->key() == Qt::Key_Escape) {
        if (mMode == Mode::Insert) {
            QTextCursor cursor = mEditor->textCursor();
            if (cursor.positionInBlock() > 0)
                cursor.movePosition(QTextCursor::PreviousCharacter);
            mEditor->setTextCursor(cursor);
        }
        enterMode(Mode::Normal);
        finishCommandRecording();
        return true;
    }

    if (mMode == Mode::Insert) {
        if (mRecordingCommand)
            recordInsertKey(event);
        return false;
    }

    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_R) {
        mEditor->redo();
        normalizeNormalCursor();
        return true;
    }
    if (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::ShiftModifier)
        return false;

    QString key = event->text();
    if (key.isEmpty())
        return true;

    if (mMode != Mode::Normal)
        return handleVisual(key);

    const bool recordable = key != QStringLiteral("u") && key != QStringLiteral(".") && key != QStringLiteral("n")
        && key != QStringLiteral("N") && key != QStringLiteral("*") && key != QStringLiteral("#")
        && key != QStringLiteral("/") && key != QStringLiteral("?");
    if (mRecordingCommand && (key == QStringLiteral("u") || key == QStringLiteral("."))) {
        mRecordingCommand = false;
        mRecordingInsert = false;
        mCommandKeys.clear();
        mCommandInsertText.clear();
    }
    if (!mRecordingCommand && !mReplaying && recordable)
        beginCommandRecording(key);
    else if (mRecordingCommand && !mRecordingInsert)
        mCommandKeys += key;

    const bool handled = handleNormal(key);
    if (mRecordingCommand && mMode == Mode::Normal && !commandPending() && !mSearching)
        finishCommandRecording();
    return handled;
}

bool VimModeController::shouldOverrideShortcut(const QKeyEvent* event) const {
    if (!mEnabled)
        return false;
    if (event->key() == Qt::Key_Escape)
        return true;
    if (mMode == Mode::Insert)
        return false;
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_R)
        return true;
    if (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::ShiftModifier)
        return false;
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        return mSearching;
    return !event->text().isEmpty();
}

void VimModeController::enterMode(Mode mode) {
    if (mMode == Mode::Insert && mode != Mode::Insert)
        endChangeEditBlock();
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
    if (mode == Mode::Insert && mRecordingCommand)
        mRecordingInsert = true;
    updateCursorAppearance();
    notifyMode();
}

void VimModeController::notifyMode() { notifyStatus(modeName()); }

void VimModeController::notifyStatus(const QString& text) {
    if (MainWindow::instance() && mEditor->hasFocus())
        MainWindow::instance()->updateVimStatus(text, mEnabled);
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

void VimModeController::openLine(bool above, int count) {
    QTextCursor cursor = mEditor->textCursor();
    beginChangeEditBlock();
    if (above) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        for (int i = 0; i < count; ++i)
            cursor.insertBlock();
        cursor.movePosition(QTextCursor::PreviousBlock);
    } else {
        cursor.movePosition(QTextCursor::EndOfBlock);
        for (int i = 0; i < count; ++i)
            cursor.insertBlock();
    }
    mEditor->setTextCursor(cursor);
    enterMode(Mode::Insert);
}

int VimModeController::takeCount() {
    const int result = mCount > 0 ? mCount : 1;
    mCount = 0;
    return result;
}

bool VimModeController::move(const QString& key, QTextCursor& cursor, QTextCursor::MoveMode mode, int count) {
    if (count < 1)
        count = 1;
    QTextCursor::MoveOperation op = QTextCursor::NoMove;
    if (key == "h") {
        const int destination = qMax(cursor.block().position(), cursor.position() - count);
        cursor.setPosition(destination, mode);
        return true;
    } else if (key == "l") {
        const int destination = qMin(cursor.block().position() + cursor.block().length() - 1, cursor.position() + count);
        cursor.setPosition(destination, mode);
        return true;
    } else if (key == "j") op = QTextCursor::Down;
    else if (key == "k") op = QTextCursor::Up;
    else if (key == "w") op = QTextCursor::NextWord;
    else if (key == "b") op = QTextCursor::PreviousWord;
    else if (key == "e") op = QTextCursor::EndOfWord;
    else if (key == "0") { cursor.movePosition(QTextCursor::StartOfBlock, mode); return true; }
    else if (key == "$") {
        cursor.movePosition(QTextCursor::Down, mode, count - 1);
        cursor.movePosition(QTextCursor::EndOfBlock, mode);
        return true;
    }
    else if (key == "G") { cursor.movePosition(QTextCursor::End, mode); return true; }
    else if (key == "gg") { cursor.movePosition(QTextCursor::Start, mode); return true; }
    else if (key == "^") {
        cursor.movePosition(QTextCursor::Down, mode, count - 1);
        cursor.movePosition(QTextCursor::StartOfBlock, mode);
        while (!cursor.atBlockEnd() && cursor.document()->characterAt(cursor.position()).isSpace())
            cursor.movePosition(QTextCursor::NextCharacter, mode);
        return true;
    } else if (key == "W" || key == "B" || key == "E") {
        const QString text = cursor.document()->toPlainText();
        int position = cursor.position();
        for (int n = 0; n < count; ++n) {
            if (key == "W") {
                while (position < text.size() && !text.at(position).isSpace()) ++position;
                while (position < text.size() && text.at(position).isSpace()) ++position;
            } else if (key == "B") {
                if (position > 0) --position;
                while (position > 0 && text.at(position).isSpace()) --position;
                while (position > 0 && !text.at(position - 1).isSpace()) --position;
            } else {
                if (position < text.size() && text.at(position).isSpace())
                    while (position < text.size() && text.at(position).isSpace()) ++position;
                while (position + 1 < text.size() && !text.at(position + 1).isSpace()) ++position;
            }
        }
        cursor.setPosition(qBound(0, position, cursor.document()->characterCount() - 1), mode);
        return true;
    } else if (key == "%") {
        const QString text = cursor.document()->toPlainText();
        int position = cursor.position();
        while (position < text.size() && QStringLiteral("()[]{}").indexOf(text.at(position)) < 0
               && text.at(position) != QLatin1Char('\n'))
            ++position;
        if (position >= text.size() || text.at(position) == QLatin1Char('\n'))
            return true;
        const QChar ch = text.at(position);
        const QString opens = QStringLiteral("([{");
        const QString closes = QStringLiteral(")]}");
        int type = opens.indexOf(ch);
        int direction = 1;
        if (type < 0) { type = closes.indexOf(ch); direction = -1; }
        int depth = 1;
        for (int p = position + direction; p >= 0 && p < text.size(); p += direction) {
            if (text.at(p) == (direction > 0 ? opens.at(type) : closes.at(type))) ++depth;
            else if (text.at(p) == (direction > 0 ? closes.at(type) : opens.at(type)) && --depth == 0) {
                cursor.setPosition(p, mode);
                break;
            }
        }
        return true;
    } else return false;
    cursor.movePosition(op, mode, count);
    return true;
}

bool VimModeController::moveCharacter(QTextCursor& cursor, QChar target, CharCommand command, int count) {
    const QString line = cursor.block().text();
    int position = cursor.positionInBlock();
    int found = -1;
    if (command == CharCommand::FindForward || command == CharCommand::TillForward) {
        for (int n = 0; n < count; ++n) {
            found = line.indexOf(target, position + 1);
            if (found < 0) return false;
            position = found;
        }
        if (command == CharCommand::TillForward) --position;
    } else {
        for (int n = 0; n < count; ++n) {
            found = line.lastIndexOf(target, position - 1);
            if (found < 0) return false;
            position = found;
        }
        if (command == CharCommand::TillBackward) ++position;
    }
    cursor.setPosition(cursor.block().position() + position);
    return true;
}

bool VimModeController::handleNormal(const QString& key) {
    if (mPendingChar != CharCommand::None) {
        const CharCommand command = mPendingChar;
        mPendingChar = CharCommand::None;
        const int count = mOperatorCount * takeCount();
        mOperatorCount = 1;
        if (command == CharCommand::Replace) {
            replaceCharacters(key.at(0), count);
            return true;
        }
        mLastCharCommand = command;
        mLastFindCharacter = key.at(0);
        QTextCursor cursor = mEditor->textCursor();
        const int origin = cursor.position();
        if (moveCharacter(cursor, key.at(0), command, count)) {
            if (mPendingOperator != Operator::None) {
                const Operator op = mPendingOperator;
                mPendingOperator = Operator::None;
                applyRangeOperator(op, origin, cursor.position(), true);
            } else {
                mEditor->setTextCursor(cursor);
                normalizeNormalCursor();
            }
        } else
            mPendingOperator = Operator::None;
        return true;
    }
    if (mPendingLine != LineCommand::None) {
        const LineCommand command = mPendingLine;
        mPendingLine = LineCommand::None;
        const QChar expected = command == LineCommand::Indent ? QLatin1Char('>')
            : command == LineCommand::Unindent ? QLatin1Char('<') : QLatin1Char('=');
        if (key.at(0) == expected)
            indentLines(command, takeCount());
        else
            mCount = 0;
        return true;
    }
    if (mPendingTextObject) {
        const bool inner = mTextObjectInner;
        mPendingTextObject = false;
        const Operator op = mPendingOperator;
        mPendingOperator = Operator::None;
        const int count = mOperatorCount * takeCount();
        mOperatorCount = 1;
        applyTextObject(op, inner, key.at(0), count, false);
        return true;
    }
    if (mPendingG) {
        mPendingG = false;
        const int count = mOperatorCount * takeCount();
        mOperatorCount = 1;
        if (key == "g") {
            if (mPendingOperator != Operator::None) {
                const Operator op = mPendingOperator;
                mPendingOperator = Operator::None;
                applyLineOperatorTo(op, count - 1);
            } else {
                QTextCursor cursor = mEditor->textCursor();
                if (count == 1)
                    move("gg", cursor, QTextCursor::MoveAnchor);
                else {
                    cursor.movePosition(QTextCursor::Start);
                    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, count - 1);
                }
                mEditor->setTextCursor(cursor);
                normalizeNormalCursor();
            }
        } else {
            mPendingOperator = Operator::None;
        }
        return true;
    }
    if (mPendingOperator != Operator::None) {
        if (key.size() == 1 && key.at(0).isDigit() && (key != "0" || mCount > 0)) {
            mCount = qMin(999999, mCount * 10 + key.toInt());
            return true;
        }
        const Operator op = mPendingOperator;
        const QString operatorKey = op == Operator::Delete ? "d" : op == Operator::Change ? "c" : "y";
        if (key == operatorKey) {
            mPendingOperator = Operator::None;
            const int count = mOperatorCount * takeCount();
            mOperatorCount = 1;
            applyLineOperator(op, count);
        } else if (key == "i" || key == "a") {
            mPendingTextObject = true;
            mTextObjectInner = key == "i";
        } else if (key == "f" || key == "F" || key == "t" || key == "T") {
            mPendingChar = key == "f" ? CharCommand::FindForward : key == "F" ? CharCommand::FindBackward
                : key == "t" ? CharCommand::TillForward : CharCommand::TillBackward;
        } else if (key == "g") {
            mPendingG = true;
        } else {
            mPendingOperator = Operator::None;
            const int count = mOperatorCount * takeCount();
            mOperatorCount = 1;
            applyOperator(op, key, count);
        }
        return true;
    }

    if (key.size() == 1 && key.at(0).isDigit() && (key != "0" || mCount > 0)) {
        mCount = qMin(999999, mCount * 10 + key.toInt());
        return true;
    }
    if (key == "g") { mPendingG = true; return true; }
    if (key == "/" || key == "?") { mCount = 0; beginSearch(key == "/"); return true; }
    if (key == "n" || key == "N") {
        const int count = takeCount();
        if (!sSearchPattern.isEmpty()) {
            const bool forward = key == "n" ? sSearchForward : !sSearchForward;
            for (int i = 0; i < count; ++i)
                performSearch(sSearchPattern, forward, mEditor->textCursor().position());
        }
        return true;
    }
    if (key == "*" || key == "#") {
        const int count = takeCount();
        if (searchWord(key == "*"))
            for (int i = 1; i < count; ++i)
                performSearch(sSearchPattern, key == "*", mEditor->textCursor().position());
        return true;
    }
    if (key == ";" || key == ",") {
        const int count = takeCount();
        if (mLastCharCommand != CharCommand::None) {
            CharCommand command = mLastCharCommand;
            if (key == ",") {
                if (command == CharCommand::FindForward) command = CharCommand::FindBackward;
                else if (command == CharCommand::FindBackward) command = CharCommand::FindForward;
                else if (command == CharCommand::TillForward) command = CharCommand::TillBackward;
                else if (command == CharCommand::TillBackward) command = CharCommand::TillForward;
            }
            QTextCursor cursor = mEditor->textCursor();
            if (moveCharacter(cursor, mLastFindCharacter, command, count))
                mEditor->setTextCursor(cursor);
            normalizeNormalCursor();
        }
        return true;
    }

    const bool explicitCount = mCount > 0;
    const int count = takeCount();
    if (key == "i") { enterInsertAt(); return true; }
    if (key == "I") { enterInsertAt(QTextCursor::StartOfBlock); return true; }
    if (key == "a") { enterInsertAt(QTextCursor::NextCharacter); return true; }
    if (key == "A") { enterInsertAt(QTextCursor::EndOfBlock); return true; }
    if (key == "o") { openLine(false, count); return true; }
    if (key == "O") { openLine(true, count); return true; }
    if (key == "v") { enterMode(Mode::Visual); return true; }
    if (key == "V") { enterMode(Mode::VisualLine); return true; }
    if (key == "x") { deleteCharacter(count); return true; }
    if (key == "X") { deleteCharacter(count, true); return true; }
    if (key == "s") { beginChangeEditBlock(); deleteCharacter(count); enterMode(Mode::Insert); return true; }
    if (key == "S") { applyLineOperator(Operator::Change, count); return true; }
    if (key == "r") { mPendingChar = CharCommand::Replace; mCount = count; return true; }
    if (key == "~") { toggleCase(count); return true; }
    if (key == "J") { joinLines(count); return true; }
    if (key == "f" || key == "F" || key == "t" || key == "T") {
        mPendingChar = key == "f" ? CharCommand::FindForward : key == "F" ? CharCommand::FindBackward
            : key == "t" ? CharCommand::TillForward : CharCommand::TillBackward;
        mCount = count;
        return true;
    }
    if (key == ">" || key == "<" || key == "=") {
        mPendingLine = key == ">" ? LineCommand::Indent : key == "<" ? LineCommand::Unindent : LineCommand::Reindent;
        mCount = count;
        return true;
    }
    if (key == "D") { applyOperator(Operator::Delete, "$", count); return true; }
    if (key == "C") { applyOperator(Operator::Change, "$", count); return true; }
    if (key == "Y") { applyLineOperator(Operator::Yank, count); return true; }
    if (key == "p") { paste(false, count); return true; }
    if (key == "P") { paste(true, count); return true; }
    if (key == "u") { mEditor->undo(); normalizeNormalCursor(); return true; }
    if (key == ".") { repeatLastChange(count); return true; }
    if (key == "d" || key == "c" || key == "y") {
        mPendingOperator = key == "d" ? Operator::Delete : key == "c" ? Operator::Change : Operator::Yank;
        mOperatorCount = count;
        return true;
    }

    QTextCursor cursor = mEditor->textCursor();
    if (key == "G" && explicitCount) {
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, count - 1);
        mEditor->setTextCursor(cursor);
        normalizeNormalCursor();
        updateCursorAppearance();
    } else if (move(key, cursor, QTextCursor::MoveAnchor, count)) {
        mEditor->setTextCursor(cursor);
        normalizeNormalCursor();
        updateCursorAppearance();
    }
    return true;
}

bool VimModeController::applyOperator(Operator op, const QString& motion, int count) {
    const int currentBlock = mEditor->textCursor().blockNumber();
    if (motion == "j") {
        const int targetBlock = currentBlock + count;
        if (targetBlock < mEditor->document()->blockCount())
            applyLineOperatorTo(op, targetBlock);
        return true;
    }
    if (motion == "k") {
        const int targetBlock = currentBlock - count;
        if (targetBlock >= 0)
            applyLineOperatorTo(op, targetBlock);
        return true;
    }
    if (motion == "G") {
        const int targetBlock = count > 1 ? count - 1 : mEditor->document()->blockCount() - 1;
        applyLineOperatorTo(op, targetBlock);
        return true;
    }
    QTextCursor cursor = mEditor->textCursor();
    const int origin = cursor.position();
    if (!move(motion, cursor, QTextCursor::MoveAnchor, count))
        return false;
    const int destination = cursor.position();
    if (destination == origin)
        return true;
    applyRangeOperator(op, origin, destination, motion == "e" || motion == "E" || motion == "%");
    return true;
}

void VimModeController::applyRangeOperator(Operator op, int origin, int destination, bool inclusive) {
    if (destination > origin && inclusive)
        ++destination;
    else if (destination < origin && inclusive)
        ++origin;
    QTextCursor cursor(mEditor->document());
    cursor.setPosition(qBound(0, origin, cursor.document()->characterCount() - 1));
    cursor.setPosition(qBound(0, destination, cursor.document()->characterCount() - 1), QTextCursor::KeepAnchor);
    sRegister = cursor.selectedText().replace(QChar::ParagraphSeparator, QChar('\n'));
    sRegisterLinewise = false;
    const int start = cursor.selectionStart();
    if (op != Operator::Yank) {
        if (op == Operator::Change)
            beginChangeEditBlock();
        else
            cursor.beginEditBlock();
        cursor.removeSelectedText();
        if (op != Operator::Change)
            cursor.endEditBlock();
    } else
        cursor.setPosition(start);
    mEditor->setTextCursor(cursor);
    if (op == Operator::Change)
        enterMode(Mode::Insert);
    else
        normalizeNormalCursor();
}

void VimModeController::applyLineOperator(Operator op, int count) {
    const int currentBlock = mEditor->textCursor().blockNumber();
    applyLineOperatorTo(op, currentBlock + count - 1);
}

void VimModeController::applyLineOperatorTo(Operator op, int targetBlock) {
    QTextCursor cursor = mEditor->textCursor();
    const int currentBlock = cursor.blockNumber();
    const int firstBlock = qMin(currentBlock, qMax(0, targetBlock));
    const int lastBlock = qMax(currentBlock, qMax(0, targetBlock));
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, firstBlock);
    cursor.movePosition(QTextCursor::StartOfBlock);
    const int start = cursor.position();
    for (int i = firstBlock; i < lastBlock && cursor.block().next().isValid(); ++i)
        cursor.movePosition(QTextCursor::NextBlock);
    const int selectedLastBlock = cursor.blockNumber();
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
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, selectedLastBlock - firstBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        beginChangeEditBlock();
        cursor.removeSelectedText();
    } else if (op == Operator::Delete) {
        const int documentEnd = cursor.document()->characterCount() - 1;
        if (start > 0 && cursor.selectionEnd() == documentEnd) {
            cursor.setPosition(start - 1);
            cursor.setPosition(documentEnd, QTextCursor::KeepAnchor);
        }
        cursor.beginEditBlock();
        cursor.removeSelectedText();
        cursor.endEditBlock();
    } else
        cursor.setPosition(start);
    mEditor->setTextCursor(cursor);
    if (op == Operator::Change)
        enterMode(Mode::Insert);
    else
        normalizeNormalCursor();
}

void VimModeController::deleteCharacter(int count, bool backwards) {
    QTextCursor cursor = mEditor->textCursor();
    if (backwards) {
        for (int i = 0; i < count && cursor.positionInBlock() > 0; ++i)
            cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
    } else {
        for (int i = 0; i < count && !cursor.atEnd() && !cursor.atBlockEnd(); ++i)
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    }
    if (!cursor.hasSelection())
        return;
    sRegister = cursor.selectedText();
    sRegisterLinewise = false;
    cursor.removeSelectedText();
    mEditor->setTextCursor(cursor);
    normalizeNormalCursor();
}

void VimModeController::paste(bool before, int count) {
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
            else {
                QString contents = sRegister;
                contents.chop(1);
                for (int i = 0; i < count; ++i) {
                    cursor.insertBlock();
                    cursor.insertText(contents);
                }
                cursor.endEditBlock();
                mEditor->setTextCursor(cursor);
                normalizeNormalCursor();
                return;
            }
        }
        for (int i = 0; i < count; ++i)
            cursor.insertText(sRegister);
    } else {
        if (!before && !cursor.atBlockEnd())
            cursor.movePosition(QTextCursor::NextCharacter);
        for (int i = 0; i < count; ++i)
            cursor.insertText(sRegister);
        cursor.movePosition(QTextCursor::PreviousCharacter);
    }
    cursor.endEditBlock();
    mEditor->setTextCursor(cursor);
    normalizeNormalCursor();
}

void VimModeController::replaceCharacters(QChar value, int count) {
    QTextCursor cursor = mEditor->textCursor();
    const int start = cursor.position();
    for (int i = 0; i < count && !cursor.atBlockEnd(); ++i)
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    if (!cursor.hasSelection()) return;
    const int length = cursor.selectionEnd() - cursor.selectionStart();
    cursor.insertText(QString(length, value));
    cursor.setPosition(start + length - 1);
    mEditor->setTextCursor(cursor);
}

void VimModeController::toggleCase(int count) {
    QTextCursor cursor = mEditor->textCursor();
    const int start = cursor.position();
    for (int i = 0; i < count && !cursor.atBlockEnd(); ++i)
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    for (int i = 0; i < text.size(); ++i)
        text[i] = text.at(i).isUpper() ? text.at(i).toLower() : text.at(i).toUpper();
    cursor.insertText(text);
    cursor.setPosition(qMin(start + text.size(), cursor.document()->characterCount() - 1));
    mEditor->setTextCursor(cursor);
    normalizeNormalCursor();
}

void VimModeController::joinLines(int count) {
    QTextCursor cursor = mEditor->textCursor();
    const int origin = cursor.position();
    const int joins = qMax(1, count - 1);
    cursor.beginEditBlock();
    for (int i = 0; i < joins; ++i) {
        cursor.movePosition(QTextCursor::EndOfBlock);
        if (cursor.atEnd()) break;
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        while (!cursor.atEnd() && cursor.document()->characterAt(cursor.position()).isSpace()) {
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
        if (cursor.position() > 0 && !cursor.document()->characterAt(cursor.position() - 1).isSpace())
            cursor.insertText(QStringLiteral(" "));
    }
    cursor.endEditBlock();
    cursor.setPosition(qMin(origin, cursor.document()->characterCount() - 1));
    mEditor->setTextCursor(cursor);
    normalizeNormalCursor();
}

void VimModeController::indentLines(LineCommand command, int count) {
    QTextCursor cursor = mEditor->textCursor();
    const int originBlock = cursor.blockNumber();
    ScCodeEditor* scEditor = dynamic_cast<ScCodeEditor*>(mEditor);
    if (command == LineCommand::Reindent && scEditor) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, count - 1);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        scEditor->setTextCursor(cursor);
        scEditor->indent();
        cursor = scEditor->textCursor();
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, originBlock);
        scEditor->setTextCursor(cursor);
        normalizeNormalCursor();
        return;
    }
    const QString indentation = scEditor ? scEditor->makeIndentationString(1) : QStringLiteral("    ");
    cursor.beginEditBlock();
    for (int i = 0; i < count && cursor.block().isValid(); ++i) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        if (command == LineCommand::Indent) {
            cursor.insertText(indentation);
        } else if (command == LineCommand::Unindent) {
            const QString text = cursor.block().text();
            int remove = text.startsWith(QLatin1Char('\t')) ? 1 : 0;
            while (remove < qMin(indentation.size(), text.size()) && text.at(remove) == QLatin1Char(' ')) ++remove;
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, remove);
            cursor.removeSelectedText();
        } else {
            const QString text = cursor.block().text();
            int leading = 0;
            while (leading < text.size() && text.at(leading).isSpace()) ++leading;
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, leading);
            cursor.removeSelectedText();
            QTextBlock previous = cursor.block().previous();
            if (previous.isValid()) {
                const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^\\s*")).match(previous.text());
                cursor.insertText(match.captured());
            }
        }
        if (i + 1 < count && !cursor.movePosition(QTextCursor::NextBlock)) break;
    }
    cursor.endEditBlock();
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, originBlock);
    mEditor->setTextCursor(cursor);
    normalizeNormalCursor();
}

bool VimModeController::selectTextObject(QTextCursor& cursor, bool inner, QChar object, int count) {
    const QString text = cursor.document()->toPlainText();
        int position = qMin(cursor.position(), text.size() - 1);
    int start = -1;
    int end = -1;
    if (object == QLatin1Char('w')) {
        if (position < 0) return false;
        if (!isWordCharacter(text.at(position)) && position > 0 && isWordCharacter(text.at(position - 1))) --position;
        if (!isWordCharacter(text.at(position))) return false;
        start = position;
        while (start > 0 && isWordCharacter(text.at(start - 1))) --start;
        end = position + 1;
        while (end < text.size() && isWordCharacter(text.at(end))) ++end;
        for (int n = 1; n < count; ++n) {
            while (end < text.size() && !isWordCharacter(text.at(end))) ++end;
            while (end < text.size() && isWordCharacter(text.at(end))) ++end;
        }
        if (!inner) {
            int trailing = end;
            while (trailing < text.size() && text.at(trailing).isSpace() && text.at(trailing) != QLatin1Char('\n')) ++trailing;
            if (trailing > end) end = trailing;
            else while (start > 0 && text.at(start - 1).isSpace() && text.at(start - 1) != QLatin1Char('\n')) --start;
        }
    } else if (QStringLiteral("([{\"").contains(object) || object == QLatin1Char('\'')) {
        if (object == QLatin1Char('"') || object == QLatin1Char('\'')) {
            const QTextBlock block = cursor.block();
            const QString line = block.text();
            int local = qMin(cursor.position() - block.position(), line.size() - 1);
            if (local < 0) return false;
            QList<int> quotes;
            for (int p = 0; p < line.size(); ++p)
                if (line.at(p) == object && !isEscaped(line, p)) quotes.append(p);
            for (int i = 0; i + 1 < quotes.size(); i += 2) {
                if (quotes.at(i) <= local && local <= quotes.at(i + 1)) {
                    start = block.position() + quotes.at(i);
                    end = block.position() + quotes.at(i + 1) + 1;
                    break;
                }
            }
        } else {
            const QString opens = QStringLiteral("([{");
            const QString closes = QStringLiteral(")]}");
            const int type = opens.indexOf(object);
            int depth = 0;
            for (int p = position; p >= 0; --p) {
                if (text.at(p) == closes.at(type)) ++depth;
                else if (text.at(p) == opens.at(type)) {
                    if (depth == 0) { start = p; break; }
                    --depth;
                }
            }
            if (start >= 0) {
                depth = 0;
                for (int p = start; p < text.size(); ++p) {
                    if (text.at(p) == opens.at(type)) ++depth;
                    else if (text.at(p) == closes.at(type) && --depth == 0) { end = p + 1; break; }
                }
            }
        }
        if (start >= 0 && end > start && inner) { ++start; --end; }
    }
    if (start < 0 || end < start)
        return false;
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    return true;
}

bool VimModeController::applyTextObject(Operator op, bool inner, QChar object, int count, bool visual) {
    QTextCursor cursor = mEditor->textCursor();
    if (!selectTextObject(cursor, inner, object, count))
        return false;
    mEditor->setTextCursor(cursor);
    if (visual) {
        mMode = Mode::Visual;
        mVisualAnchor = cursor.selectionStart();
        notifyMode();
    } else
        applySelectionOperator(op);
    return true;
}

void VimModeController::updateVisualSelection() {
    QTextCursor cursor = mEditor->textCursor();
    int active = cursor.position();
    if (mMode == Mode::VisualLine) {
        QTextCursor anchorCursor(cursor.document());
        anchorCursor.setPosition(mVisualAnchor);
        anchorCursor.movePosition(QTextCursor::StartOfBlock);
        const int anchorStart = anchorCursor.position();
        anchorCursor.movePosition(QTextCursor::EndOfBlock);
        if (!anchorCursor.atEnd()) anchorCursor.movePosition(QTextCursor::NextCharacter);
        cursor.clearSelection();
        cursor.setPosition(active);
        cursor.movePosition(QTextCursor::StartOfBlock);
        const int activeStart = cursor.position();
        cursor.movePosition(QTextCursor::EndOfBlock);
        if (!cursor.atEnd()) cursor.movePosition(QTextCursor::NextCharacter);
        const int activeEnd = cursor.position();
        if (activeStart < anchorStart) {
            cursor.setPosition(anchorCursor.position());
            cursor.setPosition(activeStart, QTextCursor::KeepAnchor);
        } else {
            cursor.setPosition(anchorStart);
            cursor.setPosition(activeEnd, QTextCursor::KeepAnchor);
        }
    } else {
        const int documentEnd = cursor.document()->characterCount() - 1;
        if (active < mVisualAnchor) {
            cursor.setPosition(qMin(mVisualAnchor + 1, documentEnd));
            cursor.setPosition(active, QTextCursor::KeepAnchor);
        } else {
            cursor.setPosition(mVisualAnchor);
            cursor.setPosition(qMin(active + 1, documentEnd), QTextCursor::KeepAnchor);
        }
    }
    mEditor->setTextCursor(cursor);
}

bool VimModeController::handleVisual(const QString& key) {
    if (mPendingChar != CharCommand::None) {
        CharCommand command = mPendingChar;
        mPendingChar = CharCommand::None;
        QTextCursor cursor = mEditor->textCursor();
        int active = cursor.position() == cursor.selectionStart() ? cursor.selectionStart() : cursor.selectionEnd() - 1;
        cursor.clearSelection();
        cursor.setPosition(active);
        if (moveCharacter(cursor, key.at(0), command, takeCount())) {
            mLastCharCommand = command;
            mLastFindCharacter = key.at(0);
            mEditor->setTextCursor(cursor);
            updateVisualSelection();
        }
        return true;
    }
    if (mPendingTextObject) {
        const bool inner = mTextObjectInner;
        mPendingTextObject = false;
        return applyTextObject(Operator::None, inner, key.at(0), takeCount(), true);
    }
    if (key.size() == 1 && key.at(0).isDigit() && (key != "0" || mCount > 0)) {
        mCount = qMin(999999, mCount * 10 + key.toInt());
        return true;
    }
    if (key == "i" || key == "a") { mPendingTextObject = true; mTextObjectInner = key == "i"; return true; }
    if (key == "f" || key == "F" || key == "t" || key == "T") {
        mPendingChar = key == "f" ? CharCommand::FindForward : key == "F" ? CharCommand::FindBackward
            : key == "t" ? CharCommand::TillForward : CharCommand::TillBackward;
        return true;
    }
    if (key == "v" && mMode == Mode::Visual) { enterMode(Mode::Normal); return true; }
    if (key == "V") {
        if (mMode == Mode::VisualLine) enterMode(Mode::Normal);
        else { mMode = Mode::VisualLine; updateVisualSelection(); notifyMode(); }
        return true;
    }
    if (key == "d" || key == "x" || key == "c" || key == "y" || key == "p") {
        Operator op = key == "y" ? Operator::Yank : key == "c" ? Operator::Change : Operator::Delete;
        if (key == "p") {
            const QString contents = sRegister;
            const bool linewise = sRegisterLinewise;
            applySelectionOperator(Operator::Delete);
            sRegister = contents;
            sRegisterLinewise = linewise;
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
    if (move(motion, cursor, QTextCursor::MoveAnchor, takeCount())) {
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
        if (op == Operator::Change)
            beginChangeEditBlock();
        else
            cursor.beginEditBlock();
        if (op == Operator::Change && sRegisterLinewise && cursor.selectionEnd() > cursor.selectionStart()
            && cursor.document()->characterAt(cursor.selectionEnd() - 1) == QChar::ParagraphSeparator) {
            const int start = cursor.selectionStart();
            const int end = cursor.selectionEnd() - 1;
            cursor.setPosition(start);
            cursor.setPosition(end, QTextCursor::KeepAnchor);
        }
        cursor.removeSelectedText();
        if (op != Operator::Change)
            cursor.endEditBlock();
    } else cursor.setPosition(cursor.selectionStart());
    mEditor->setTextCursor(cursor);
    if (op == Operator::Change)
        enterMode(Mode::Insert);
    else
        enterMode(Mode::Normal);
}

void VimModeController::beginSearch(bool forward) {
    mSearching = true;
    mSearchForward = forward;
    mSearchOrigin = mEditor->textCursor().position();
    mSearchInput.clear();
    notifyStatus(forward ? QStringLiteral("/") : QStringLiteral("?"));
}

bool VimModeController::handleSearchKey(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) { finishSearch(false); return true; }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { finishSearch(true); return true; }
    if (event->key() == Qt::Key_Backspace) {
        if (!mSearchInput.isEmpty()) mSearchInput.chop(1);
    } else if ((event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier)
               && !event->text().isEmpty())
        mSearchInput += event->text();
    else
        return true;
    updateSearchPreview();
    return true;
}

bool VimModeController::updateSearchPreview() {
    const QChar prefix = mSearchForward ? QLatin1Char('/') : QLatin1Char('?');
    if (mSearchInput.isEmpty()) {
        QTextCursor cursor = mEditor->textCursor();
        cursor.setPosition(mSearchOrigin);
        mEditor->setTextCursor(cursor);
        notifyStatus(QString(prefix));
        return false;
    }
    const QRegularExpression expression(mSearchInput);
    if (!expression.isValid()) {
        notifyStatus(QString(prefix) + mSearchInput + QStringLiteral(" [invalid pattern]"));
        return false;
    }
    const bool found = performSearch(mSearchInput, mSearchForward, mSearchOrigin);
    notifyStatus(QString(prefix) + mSearchInput + (found ? QString() : QStringLiteral(" [no match]")));
    return found;
}

bool VimModeController::performSearch(const QString& pattern, bool forward, int origin, bool includeOrigin) {
    const QRegularExpression expression(pattern);
    if (!expression.isValid() || pattern.isEmpty())
        return false;
    const QString text = mEditor->toPlainText();
    int result = -1;
    if (forward) {
        QRegularExpressionMatch match = expression.match(text, qMin(origin + (includeOrigin ? 0 : 1), text.size()));
        if (!match.hasMatch()) match = expression.match(text, 0);
        if (match.hasMatch()) result = match.capturedStart();
    } else {
        QRegularExpressionMatchIterator it = expression.globalMatch(text);
        int wrapped = -1;
        while (it.hasNext()) {
            const int start = it.next().capturedStart();
            if (start < origin || (includeOrigin && start == origin)) result = start;
            wrapped = start;
        }
        if (result < 0) result = wrapped;
    }
    if (result < 0)
        return false;
    QTextCursor cursor = mEditor->textCursor();
    cursor.setPosition(result);
    mEditor->setTextCursor(cursor);
    normalizeNormalCursor();
    updateCursorAppearance();
    return true;
}

void VimModeController::finishSearch(bool commit) {
    const QString pattern = mSearchInput.isEmpty() ? sSearchPattern : mSearchInput;
    if (commit && !pattern.isEmpty() && QRegularExpression(pattern).isValid()) {
        if (performSearch(pattern, mSearchForward, mSearchOrigin)) {
            sSearchPattern = pattern;
            sSearchForward = mSearchForward;
        } else {
            QTextCursor cursor = mEditor->textCursor();
            cursor.setPosition(mSearchOrigin);
            mEditor->setTextCursor(cursor);
        }
    } else {
        QTextCursor cursor = mEditor->textCursor();
        cursor.setPosition(mSearchOrigin);
        mEditor->setTextCursor(cursor);
    }
    mSearching = false;
    notifyMode();
    finishCommandRecording();
}

bool VimModeController::searchWord(bool forward) {
    const QString text = mEditor->toPlainText();
    int position = qMin(mEditor->textCursor().position(), text.size() - 1);
    if (position < 0 || !isWordCharacter(text.at(position))) return false;
    int start = position;
    int end = position + 1;
    while (start > 0 && isWordCharacter(text.at(start - 1))) --start;
    while (end < text.size() && isWordCharacter(text.at(end))) ++end;
    sSearchPattern = QStringLiteral("\\b") + QRegularExpression::escape(text.mid(start, end - start)) + QStringLiteral("\\b");
    sSearchForward = forward;
    return performSearch(sSearchPattern, forward, position);
}

void VimModeController::beginCommandRecording(const QString& key) {
    mRecordingCommand = true;
    mRecordingInsert = false;
    mCommandRevision = mEditor->document()->revision();
    mCommandKeys = key;
    mCommandInsertText.clear();
}

void VimModeController::recordInsertKey(QKeyEvent* event) {
    if (event->key() == Qt::Key_Backspace) {
        if (!mCommandInsertText.isEmpty()) mCommandInsertText.chop(1);
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        mCommandInsertText += QLatin1Char('\n');
    else if (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier)
        mCommandInsertText += event->text();
}

void VimModeController::finishCommandRecording() {
    if (!mRecordingCommand || mReplaying)
        return;
    if (mEditor->document()->revision() != mCommandRevision) {
        mLastChangeKeys = mCommandKeys;
        mLastChangeInsertText = mCommandInsertText;
    }
    mRecordingCommand = false;
    mRecordingInsert = false;
    mCommandKeys.clear();
    mCommandInsertText.clear();
}

void VimModeController::repeatLastChange(int count) {
    if (mLastChangeKeys.isEmpty())
        return;
    mReplaying = true;
    for (int repetition = 0; repetition < count; ++repetition) {
        for (QChar key : mLastChangeKeys)
            handleNormal(QString(key));
        if (mMode == Mode::Insert) {
            QTextCursor cursor = mEditor->textCursor();
            cursor.insertText(mLastChangeInsertText);
            if (cursor.positionInBlock() > 0)
                cursor.movePosition(QTextCursor::PreviousCharacter);
            mEditor->setTextCursor(cursor);
            enterMode(Mode::Normal);
        }
    }
    mReplaying = false;
}

void VimModeController::beginChangeEditBlock() {
    if (mChangeEditBlockOpen)
        return;
    QTextCursor cursor(mEditor->document());
    cursor.beginEditBlock();
    mChangeEditBlockOpen = true;
}

void VimModeController::endChangeEditBlock() {
    if (!mChangeEditBlockOpen)
        return;
    QTextCursor cursor(mEditor->document());
    cursor.endEditBlock();
    mChangeEditBlockOpen = false;
}

} // namespace ScIDE
