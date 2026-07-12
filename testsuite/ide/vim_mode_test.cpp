#include "widgets/code_editor/vim_mode.hpp"

#include <boost/test/unit_test.hpp>
#include <QApplication>
#include <QKeyEvent>
#include <QPlainTextEdit>

using ScIDE::VimModeController;

namespace {

QApplication* application() {
    static int argc = 1;
    static char name[] = "vim_mode_test";
    static char* argv[] = { name, nullptr };
    static QApplication app(argc, argv);
    return &app;
}

bool press(VimModeController& vim, int key, const QString& text = QString(),
           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    return vim.handleKeyPress(&event);
}

bool overridesShortcut(VimModeController& vim, int key, const QString& text = QString(),
                       Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::ShortcutOverride, key, modifiers, text);
    return vim.shouldOverrideShortcut(&event);
}

void pressChar(VimModeController& vim, QChar value) {
    const bool upper = value.isUpper();
    press(vim, value.toUpper().unicode(), QString(value), upper ? Qt::ShiftModifier : Qt::NoModifier);
}

void typeText(VimModeController& vim, QPlainTextEdit& editor, const QString& text) {
    for (QChar value : text) {
        if (value == QLatin1Char('\n')) {
            if (!press(vim, Qt::Key_Return, QStringLiteral("\n")))
                editor.insertPlainText(QStringLiteral("\n"));
        } else if (!press(vim, value.toUpper().unicode(), QString(value),
                          value.isUpper() ? Qt::ShiftModifier : Qt::NoModifier))
            editor.insertPlainText(QString(value));
    }
}

void backspace(VimModeController& vim, QPlainTextEdit& editor) {
    if (!press(vim, Qt::Key_Backspace)) {
        QTextCursor cursor = editor.textCursor();
        cursor.deletePreviousChar();
        editor.setTextCursor(cursor);
    }
}

struct EditorFixture {
    EditorFixture(): ignored(application()), vim(&editor) { vim.setEnabled(true); }
    QApplication* ignored;
    QPlainTextEdit editor;
    VimModeController vim;
};

class TransformingEditor : public QPlainTextEdit {
public:
    VimModeController* vim { nullptr };

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (vim && vim->handleKeyPress(event))
            return;
        if (event->key() == Qt::Key_Tab) {
            insertPlainText(QStringLiteral("  "));
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            insertPlainText(QStringLiteral("\n  "));
        } else if (event->text() == QStringLiteral("(")) {
            insertPlainText(QStringLiteral("()"));
            moveCursor(QTextCursor::PreviousCharacter);
        } else {
            QPlainTextEdit::keyPressEvent(event);
        }
    }
};

struct TransformingEditorFixture {
    TransformingEditorFixture(): ignored(application()), vim(&editor) {
        editor.vim = &vim;
        vim.setEnabled(true);
    }

    void send(int key, const QString& text = QString(), Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
        QApplication::sendEvent(&editor, &event);
    }

    void sendChar(QChar value) {
        send(value.toUpper().unicode(), QString(value), value.isUpper() ? Qt::ShiftModifier : Qt::NoModifier);
    }

    QApplication* ignored;
    TransformingEditor editor;
    VimModeController vim;
};

} // namespace

BOOST_AUTO_TEST_SUITE(vim_mode)

BOOST_FIXTURE_TEST_CASE(mode_transitions, EditorFixture) {
    editor.setPlainText("abc");
    pressChar(vim, 'i');
    BOOST_CHECK(vim.mode() == VimModeController::Mode::Insert);
    BOOST_CHECK(!press(vim, Qt::Key_X, "x"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK(vim.mode() == VimModeController::Mode::Normal);
    pressChar(vim, 'v');
    BOOST_CHECK(vim.mode() == VimModeController::Mode::Visual);
    pressChar(vim, 'V');
    BOOST_CHECK(vim.mode() == VimModeController::Mode::VisualLine);
}

BOOST_AUTO_TEST_CASE(unchanged_enabled_state_preserves_editor_state) {
    application();
    QPlainTextEdit editor;
    editor.setPlainText("abc");
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(0);
    cursor.setPosition(2, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);

    VimModeController vim(&editor);
    vim.setEnabled(false);
    BOOST_CHECK(editor.textCursor().hasSelection());

    vim.setEnabled(true);
    pressChar(vim, 'i');
    vim.setEnabled(true);
    BOOST_CHECK(vim.mode() == VimModeController::Mode::Insert);

    press(vim, Qt::Key_Escape);
    pressChar(vim, 'v');
    pressChar(vim, 'l');
    const int selectionStart = editor.textCursor().selectionStart();
    const int selectionEnd = editor.textCursor().selectionEnd();
    vim.setEnabled(true);
    BOOST_CHECK(vim.mode() == VimModeController::Mode::Visual);
    BOOST_CHECK_EQUAL(editor.textCursor().selectionStart(), selectionStart);
    BOOST_CHECK_EQUAL(editor.textCursor().selectionEnd(), selectionEnd);
}

BOOST_FIXTURE_TEST_CASE(tab_is_modal_outside_insert_mode, EditorFixture) {
    editor.setPlainText("abc");
    BOOST_CHECK(press(vim, Qt::Key_Tab, QStringLiteral("\t")));

    pressChar(vim, 'v');
    BOOST_CHECK(press(vim, Qt::Key_Tab, QStringLiteral("\t")));

    press(vim, Qt::Key_Escape);
    pressChar(vim, 'i');
    BOOST_CHECK(!press(vim, Qt::Key_Tab, QStringLiteral("\t")));
}

BOOST_FIXTURE_TEST_CASE(shortcut_override_is_mode_aware, EditorFixture) {
    editor.setPlainText("abc");
    BOOST_CHECK(overridesShortcut(vim, Qt::Key_H, QStringLiteral("h")));
    BOOST_CHECK(overridesShortcut(vim, Qt::Key_Escape));
    BOOST_CHECK(overridesShortcut(vim, Qt::Key_R, QString(), Qt::ControlModifier));
    BOOST_CHECK(!overridesShortcut(vim, Qt::Key_F5));
    BOOST_CHECK(!overridesShortcut(vim, Qt::Key_Return, QStringLiteral("\n"), Qt::ShiftModifier));

    pressChar(vim, 'i');
    BOOST_CHECK(!overridesShortcut(vim, Qt::Key_H, QStringLiteral("h")));
    BOOST_CHECK(!overridesShortcut(vim, Qt::Key_Return, QStringLiteral("\n"), Qt::ShiftModifier));
    BOOST_CHECK(overridesShortcut(vim, Qt::Key_Escape));

    press(vim, Qt::Key_Escape);
    pressChar(vim, '/');
    BOOST_CHECK(overridesShortcut(vim, Qt::Key_Return, QStringLiteral("\n")));
    BOOST_CHECK(!overridesShortcut(vim, Qt::Key_F5));
}

BOOST_FIXTURE_TEST_CASE(horizontal_motions_stay_in_current_line, EditorFixture) {
    editor.setPlainText("abc\ndef\n\nghi");
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock);
    editor.setTextCursor(cursor);

    pressChar(vim, 'h');
    BOOST_CHECK_EQUAL(editor.textCursor().blockNumber(), 1);
    BOOST_CHECK_EQUAL(editor.textCursor().positionInBlock(), 0);

    pressChar(vim, '9');
    pressChar(vim, 'l');
    BOOST_CHECK_EQUAL(editor.textCursor().blockNumber(), 1);
    BOOST_CHECK_EQUAL(editor.textCursor().positionInBlock(), 2);

    cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock);
    editor.setTextCursor(cursor);
    pressChar(vim, 'l');
    BOOST_CHECK_EQUAL(editor.textCursor().blockNumber(), 2);
    BOOST_CHECK_EQUAL(editor.textCursor().positionInBlock(), 0);
}

BOOST_FIXTURE_TEST_CASE(motions_and_operator_motion, EditorFixture) {
    editor.setPlainText("one two three");
    pressChar(vim, 'w');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 4);
    pressChar(vim, 'd');
    pressChar(vim, 'w');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one three");
    editor.undo();
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one two three");

    editor.setPlainText("replace me\nnext");
    pressChar(vim, 'c');
    pressChar(vim, 'c');
    BOOST_CHECK(vim.mode() == VimModeController::Mode::Insert);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "\nnext");
    press(vim, Qt::Key_Escape);
    BOOST_CHECK(!press(vim, Qt::Key_S, "s", Qt::ControlModifier));
}

BOOST_FIXTURE_TEST_CASE(end_of_word_stops_on_the_final_character, EditorFixture) {
    editor.setPlainText("abc def ghi");
    pressChar(vim, 'e');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 2);
    pressChar(vim, '2');
    pressChar(vim, 'e');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 10);

    editor.setPlainText("abc def");
    pressChar(vim, 'd');
    pressChar(vim, 'e');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), " def");

    editor.setPlainText("abc def");
    pressChar(vim, 'v');
    pressChar(vim, 'e');
    BOOST_CHECK_EQUAL(editor.textCursor().selectedText().toStdString(), "abc");
}

BOOST_FIXTURE_TEST_CASE(end_of_line_operators_preserve_the_separator, EditorFixture) {
    editor.setPlainText("abc\ndef");
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(1);
    editor.setTextCursor(cursor);
    pressChar(vim, 'd');
    pressChar(vim, '$');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "a\ndef");

    editor.setPlainText("abc\ndef");
    cursor = editor.textCursor();
    cursor.setPosition(1);
    editor.setTextCursor(cursor);
    pressChar(vim, 'D');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "a\ndef");

    editor.setPlainText("abc\ndef");
    cursor = editor.textCursor();
    cursor.setPosition(1);
    editor.setTextCursor(cursor);
    pressChar(vim, 'C');
    typeText(vim, editor, QStringLiteral("x"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "ax\ndef");
}

BOOST_FIXTURE_TEST_CASE(vertical_operator_motions_are_linewise, EditorFixture) {
    editor.setPlainText("abc\ndef\nghi");
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(1);
    editor.setTextCursor(cursor);
    pressChar(vim, 'd');
    pressChar(vim, 'j');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "ghi");

    editor.setPlainText("abc\ndef\nghi");
    cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock);
    cursor.movePosition(QTextCursor::NextCharacter);
    editor.setTextCursor(cursor);
    pressChar(vim, 'd');
    pressChar(vim, 'k');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "ghi");

    editor.setPlainText("abc\ndef\nghi");
    cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock);
    cursor.movePosition(QTextCursor::NextCharacter);
    editor.setTextCursor(cursor);
    pressChar(vim, 'd');
    pressChar(vim, 'G');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "abc");

    editor.setPlainText("abc\ndef\nghi");
    cursor = editor.textCursor();
    cursor.setPosition(1);
    editor.setTextCursor(cursor);
    pressChar(vim, 'c');
    pressChar(vim, 'j');
    typeText(vim, editor, QStringLiteral("x"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "x\nghi");
    pressChar(vim, 'u');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "abc\ndef\nghi");

    editor.setPlainText("one\ntwo\nthree\nfour");
    pressChar(vim, 'd');
    pressChar(vim, '2');
    pressChar(vim, 'j');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "four");

    editor.setPlainText("abc\ndef\nghi");
    cursor = editor.textCursor();
    cursor.setPosition(1);
    editor.setTextCursor(cursor);
    pressChar(vim, 'y');
    pressChar(vim, 'j');
    editor.setPlainText("last");
    pressChar(vim, 'p');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "last\nabc\ndef");
}

BOOST_FIXTURE_TEST_CASE(vertical_operator_counts_clamp_to_document_boundaries, EditorFixture) {
    editor.setPlainText("abc\ndef\nghi");
    pressChar(vim, 'd');
    pressChar(vim, '9');
    pressChar(vim, 'j');
    BOOST_CHECK(editor.toPlainText().isEmpty());

    editor.setPlainText("abc\ndef\nghi");
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    pressChar(vim, 'd');
    pressChar(vim, '9');
    pressChar(vim, 'k');
    BOOST_CHECK(editor.toPlainText().isEmpty());

    editor.setPlainText("abc\ndef\nghi");
    pressChar(vim, 'c');
    pressChar(vim, '9');
    pressChar(vim, 'j');
    typeText(vim, editor, QStringLiteral("x"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "x");

    editor.setPlainText("abc\ndef\nghi");
    cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    pressChar(vim, 'y');
    pressChar(vim, '9');
    pressChar(vim, 'k');
    editor.setPlainText("target");
    pressChar(vim, 'p');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "target\nabc\ndef\nghi");
}

BOOST_FIXTURE_TEST_CASE(append_on_empty_line_does_not_cross_to_next_line, EditorFixture) {
    editor.setPlainText("\nnext");
    pressChar(vim, 'a');
    typeText(vim, editor, QStringLiteral("x"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "x\nnext");
}

BOOST_FIXTURE_TEST_CASE(mouse_selection_is_normalized_before_normal_command, EditorFixture) {
    editor.setPlainText("abcdef");
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(1);
    cursor.setPosition(4, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);

    vim.mouseRepositioned();
    BOOST_CHECK(!editor.textCursor().hasSelection());
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 4);

    pressChar(vim, 'x');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "abcdf");
}

BOOST_FIXTURE_TEST_CASE(line_delete_yank_and_shared_paste, EditorFixture) {
    editor.setPlainText("alpha\nbeta\ngamma");
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock);
    editor.setTextCursor(cursor);
    pressChar(vim, 'y');
    pressChar(vim, 'y');

    QPlainTextEdit second;
    second.setPlainText("first\nlast");
    VimModeController other(&second);
    other.setEnabled(true);
    pressChar(other, 'p');
    BOOST_CHECK_EQUAL(second.toPlainText().toStdString(), "first\nbeta\nlast");

    pressChar(vim, 'd');
    pressChar(vim, 'd');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "alpha\ngamma");
}

BOOST_FIXTURE_TEST_CASE(operators_accept_gg_motion, EditorFixture) {
    editor.setPlainText("one\ntwo\nthree\nfour");
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, 2);
    editor.setTextCursor(cursor);
    pressChar(vim, 'd');
    pressChar(vim, 'g');
    pressChar(vim, 'g');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "four");

    editor.setPlainText("one\ntwo\nthree");
    cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock);
    editor.setTextCursor(cursor);
    pressChar(vim, 'y');
    pressChar(vim, 'g');
    pressChar(vim, 'g');
    editor.setPlainText("last");
    pressChar(vim, 'p');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "last\none\ntwo");

    editor.setPlainText("one\ntwo\nthree");
    cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock);
    editor.setTextCursor(cursor);
    pressChar(vim, 'c');
    pressChar(vim, 'g');
    pressChar(vim, 'g');
    typeText(vim, editor, QStringLiteral("changed"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "changed\nthree");
    pressChar(vim, 'u');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one\ntwo\nthree");
}

BOOST_FIXTURE_TEST_CASE(linewise_paste_after_eof_has_no_empty_trailing_line, EditorFixture) {
    editor.setPlainText("beta");
    pressChar(vim, 'y');
    pressChar(vim, 'y');

    editor.setPlainText("last");
    pressChar(vim, 'p');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "last\nbeta");

    editor.setPlainText("last");
    pressChar(vim, '2');
    pressChar(vim, 'p');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "last\nbeta\nbeta");
}

BOOST_FIXTURE_TEST_CASE(character_and_line_visual, EditorFixture) {
    editor.setPlainText("abc def\nsecond");
    pressChar(vim, 'v');
    pressChar(vim, 'e');
    pressChar(vim, 'y');
    BOOST_CHECK(vim.mode() == VimModeController::Mode::Normal);
    pressChar(vim, 'P');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "abcabc def\nsecond");

    editor.setPlainText("one\ntwo\nthree");
    pressChar(vim, 'V');
    pressChar(vim, 'j');
    pressChar(vim, 'd');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "three");
}

BOOST_FIXTURE_TEST_CASE(visual_line_change_preserves_a_replacement_line, EditorFixture) {
    editor.setPlainText("one\ntwo");
    pressChar(vim, 'V');
    pressChar(vim, 'c');
    typeText(vim, editor, QStringLiteral("x"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "x\ntwo");
    pressChar(vim, 'u');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one\ntwo");

    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock);
    editor.setTextCursor(cursor);
    pressChar(vim, 'V');
    pressChar(vim, 'c');
    typeText(vim, editor, QStringLiteral("x"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one\nx");
}

BOOST_FIXTURE_TEST_CASE(final_line_delete_removes_the_preceding_separator, EditorFixture) {
    editor.setPlainText("one\ntwo");
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock);
    editor.setTextCursor(cursor);
    pressChar(vim, 'd');
    pressChar(vim, 'd');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one");

    pressChar(vim, 'p');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one\ntwo");
}

BOOST_FIXTURE_TEST_CASE(visual_paste_preserves_register, EditorFixture) {
    editor.setPlainText("source");
    pressChar(vim, 'v');
    pressChar(vim, 'e');
    pressChar(vim, 'y');

    editor.setPlainText("target");
    pressChar(vim, 'v');
    pressChar(vim, 'e');
    pressChar(vim, 'p');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "source");

    editor.setPlainText("target");
    pressChar(vim, 'v');
    pressChar(vim, 'e');
    pressChar(vim, 'p');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "source");
}

BOOST_FIXTURE_TEST_CASE(backward_visual_includes_anchor, EditorFixture) {
    editor.setPlainText("abcd");
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(2);
    editor.setTextCursor(cursor);

    pressChar(vim, 'v');
    pressChar(vim, 'h');
    BOOST_CHECK_EQUAL(editor.textCursor().selectedText().toStdString(), "bc");
    pressChar(vim, 'd');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "ad");
}

BOOST_FIXTURE_TEST_CASE(upward_visual_line_selects_complete_lines, EditorFixture) {
    editor.setPlainText("one\ntwo\nthree");
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);

    pressChar(vim, 'V');
    pressChar(vim, 'k');
    BOOST_CHECK_EQUAL(editor.textCursor().selectedText().toStdString(), "two\u2029three");
    pressChar(vim, 'd');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one\n");
}

BOOST_FIXTURE_TEST_CASE(counts_compose_with_motions_and_operators, EditorFixture) {
    editor.setPlainText("one two three four five six seven");
    pressChar(vim, '3');
    pressChar(vim, 'w');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 14);

    editor.setPlainText("one two three four five six seven");
    pressChar(vim, '2');
    pressChar(vim, 'd');
    pressChar(vim, '3');
    pressChar(vim, 'w');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "seven");

    editor.setPlainText("one\ntwo\nthree\nfour");
    pressChar(vim, '3');
    pressChar(vim, 'd');
    pressChar(vim, 'd');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "four");

    editor.setPlainText("one\ntwo");
    pressChar(vim, '9');
    pressChar(vim, 'd');
    pressChar(vim, 'd');
    BOOST_CHECK(editor.toPlainText().isEmpty());
}

BOOST_FIXTURE_TEST_CASE(character_find_match_and_repeat, EditorFixture) {
    editor.setPlainText("a-b-c-d");
    pressChar(vim, 'f');
    pressChar(vim, '-');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 1);
    pressChar(vim, ';');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 3);
    pressChar(vim, ',');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 1);

    editor.setPlainText("(one [two] three)");
    pressChar(vim, '%');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 16);

    editor.setPlainText("abc-def-ghi");
    pressChar(vim, 'd');
    pressChar(vim, 't');
    pressChar(vim, '-');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "-def-ghi");
}

BOOST_FIXTURE_TEST_CASE(search_prompt_repeat_wrap_and_cancel, EditorFixture) {
    editor.setPlainText("one two one three");
    pressChar(vim, '/');
    pressChar(vim, 'o');
    pressChar(vim, 'n');
    pressChar(vim, 'e');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 8);
    press(vim, Qt::Key_Return, QStringLiteral("\n"));
    pressChar(vim, 'n');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 0);
    pressChar(vim, 'N');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 8);

    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(4);
    editor.setTextCursor(cursor);
    pressChar(vim, '?');
    pressChar(vim, 't');
    pressChar(vim, 'h');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 12);
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 4);

    pressChar(vim, '/');
    pressChar(vim, '[');
    press(vim, Qt::Key_Return, QStringLiteral("\n"));
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 4);
}

BOOST_FIXTURE_TEST_CASE(word_search_is_shared_between_editors, EditorFixture) {
    editor.setPlainText("alpha beta alpha");
    pressChar(vim, '*');
    BOOST_CHECK_EQUAL(editor.textCursor().position(), 11);

    QPlainTextEdit second;
    second.setPlainText("alpha elsewhere alpha");
    VimModeController other(&second);
    other.setEnabled(true);
    pressChar(other, 'n');
    BOOST_CHECK_EQUAL(second.textCursor().position(), 16);
    pressChar(other, '#');
    BOOST_CHECK_EQUAL(second.textCursor().position(), 0);
}

BOOST_FIXTURE_TEST_CASE(word_bracket_and_quote_text_objects, EditorFixture) {
    editor.setPlainText("one two three");
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(5);
    editor.setTextCursor(cursor);
    pressChar(vim, 'd');
    pressChar(vim, 'a');
    pressChar(vim, 'w');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one three");

    editor.setPlainText("call(one + two)");
    cursor = editor.textCursor();
    cursor.setPosition(7);
    editor.setTextCursor(cursor);
    pressChar(vim, 'd');
    pressChar(vim, 'i');
    pressChar(vim, '(');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "call()");

    editor.setPlainText("say 'hello world' now");
    cursor = editor.textCursor();
    cursor.setPosition(8);
    editor.setTextCursor(cursor);
    pressChar(vim, 'v');
    pressChar(vim, 'i');
    pressChar(vim, '\'');
    BOOST_CHECK_EQUAL(editor.textCursor().selectedText().toStdString(), "hello world");
}

BOOST_FIXTURE_TEST_CASE(expected_editing_commands, EditorFixture) {
    editor.setPlainText("abcd");
    pressChar(vim, '2');
    pressChar(vim, 'r');
    pressChar(vim, 'x');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "xxcd");
    pressChar(vim, '~');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "xXcd");
    pressChar(vim, 'X');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "xcd");

    editor.setPlainText("one\n   two\nthree");
    pressChar(vim, 'J');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one two\nthree");
    pressChar(vim, '>');
    pressChar(vim, '>');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "    one two\nthree");
    pressChar(vim, '<');
    pressChar(vim, '<');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one two\nthree");

    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextBlock);
    editor.setTextCursor(cursor);
    pressChar(vim, '=');
    pressChar(vim, '=');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one two\nthree");
}

BOOST_FIXTURE_TEST_CASE(counted_search_line_jump_and_visual_find, EditorFixture) {
    editor.setPlainText("hit\none hit\ntwo hit\nthree hit");
    pressChar(vim, '*');
    pressChar(vim, '2');
    pressChar(vim, 'n');
    BOOST_CHECK_EQUAL(editor.textCursor().blockNumber(), 3);

    pressChar(vim, '2');
    pressChar(vim, 'G');
    BOOST_CHECK_EQUAL(editor.textCursor().blockNumber(), 1);

    editor.setPlainText("a-b-c");
    pressChar(vim, 'v');
    pressChar(vim, 'f');
    pressChar(vim, 'b');
    BOOST_CHECK_EQUAL(editor.textCursor().selectedText().toStdString(), "a-b");
}

BOOST_FIXTURE_TEST_CASE(dot_repeats_changes_and_inserted_text, EditorFixture) {
    editor.setPlainText("one two");
    pressChar(vim, 'c');
    pressChar(vim, 'w');
    typeText(vim, editor, QStringLiteral("hi "));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "hi two");
    pressChar(vim, 'w');
    pressChar(vim, '.');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "hi hi ");

    editor.setPlainText("abcdef");
    pressChar(vim, '2');
    pressChar(vim, 'x');
    pressChar(vim, '.');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "ef");
}

BOOST_FIXTURE_TEST_CASE(dot_replays_insert_mode_backspaces_in_order, EditorFixture) {
    editor.setPlainText("abc");
    pressChar(vim, 'a');
    backspace(vim, editor);
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "bc");
    pressChar(vim, '.');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "c");

    editor.setPlainText("abc");
    pressChar(vim, 'a');
    typeText(vim, editor, QStringLiteral("x"));
    backspace(vim, editor);
    backspace(vim, editor);
    typeText(vim, editor, QStringLiteral("y"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "ybc");
    pressChar(vim, 'l');
    pressChar(vim, '.');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "yyc");
}

BOOST_FIXTURE_TEST_CASE(dot_replays_insert_keys_through_editor_behavior, TransformingEditorFixture) {
    editor.setPlainText("one");
    sendChar('A');
    send(Qt::Key_Return, QStringLiteral("\n"));
    sendChar('x');
    send(Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one\n  x");
    sendChar('.');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one\n  x\n  x");

    editor.setPlainText(QString());
    sendChar('i');
    send(Qt::Key_Tab, QStringLiteral("\t"));
    send(Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "  ");
    sendChar('.');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "    ");

    editor.setPlainText(QString());
    sendChar('i');
    sendChar('(');
    send(Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "()");
    sendChar('.');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "()()");
}

BOOST_FIXTURE_TEST_CASE(change_operator_and_insert_are_one_undo_step, EditorFixture) {
    editor.setPlainText("one two");
    pressChar(vim, 'c');
    pressChar(vim, 'w');
    typeText(vim, editor, QStringLiteral("hi "));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "hi two");

    pressChar(vim, 'u');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one two");
}

BOOST_FIXTURE_TEST_CASE(open_line_and_insert_are_one_undo_step, EditorFixture) {
    editor.setPlainText("one");
    pressChar(vim, 'o');
    typeText(vim, editor, QStringLiteral("x"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one\nx");
    pressChar(vim, 'u');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one");

    pressChar(vim, 'O');
    typeText(vim, editor, QStringLiteral("x"));
    press(vim, Qt::Key_Escape);
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "x\none");
    pressChar(vim, 'u');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "one");
}

BOOST_FIXTURE_TEST_CASE(normal_motions_do_not_replace_the_last_change, EditorFixture) {
    editor.setPlainText("abcd");
    pressChar(vim, 'x');
    pressChar(vim, 'l');
    pressChar(vim, 'h');
    pressChar(vim, '.');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "cd");
}

BOOST_AUTO_TEST_SUITE_END()
