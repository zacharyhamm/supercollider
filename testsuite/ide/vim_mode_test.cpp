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

BOOST_AUTO_TEST_SUITE_END()
