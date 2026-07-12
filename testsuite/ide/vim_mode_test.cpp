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

struct EditorFixture {
    EditorFixture(): ignored(application()), vim(&editor) { vim.setEnabled(true); }
    QApplication* ignored;
    QPlainTextEdit editor;
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

BOOST_FIXTURE_TEST_CASE(character_and_line_visual, EditorFixture) {
    editor.setPlainText("abc def\nsecond");
    pressChar(vim, 'v');
    pressChar(vim, 'e');
    pressChar(vim, 'y');
    BOOST_CHECK(vim.mode() == VimModeController::Mode::Normal);
    pressChar(vim, 'P');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "abc abc def\nsecond");

    editor.setPlainText("one\ntwo\nthree");
    pressChar(vim, 'V');
    pressChar(vim, 'j');
    pressChar(vim, 'd');
    BOOST_CHECK_EQUAL(editor.toPlainText().toStdString(), "three");
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

BOOST_AUTO_TEST_SUITE_END()
