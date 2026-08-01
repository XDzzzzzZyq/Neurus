#pragma once

#include <QPlainTextEdit>
#include <memory>
#include <string>

#include "ShaderHighlighter.h"

namespace neurus {

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    using Language = ShaderHighlighter::Language;

    explicit CodeEditor(QWidget* parent = nullptr);
    ~CodeEditor() override = default;

    CodeEditor(const CodeEditor&) = delete;
    CodeEditor& operator=(const CodeEditor&) = delete;

    void setLanguage(Language lang);
    void setReadOnly(bool readOnly);
    void setCode(const std::string& code);
    std::string getCode() const;

    void setLineNumbersVisible(bool visible);
    bool lineNumbersVisible() const { return m_lineNumbersVisible; }

signals:
    void codeChanged(const std::string& code);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void updateLineNumberAreaWidth();
    int  lineNumberAreaWidth() const;

    class LineNumberArea : public QWidget {
    public:
        explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), m_editor(editor) {}
        QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }
    protected:
        void paintEvent(QPaintEvent* event) override { m_editor->lineNumberAreaPaintEvent(event); }
    private:
        CodeEditor* m_editor;
    };
    friend class LineNumberArea;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

    LineNumberArea*              m_lineNumberArea      = nullptr;
    std::unique_ptr<ShaderHighlighter> m_highlighter;
    bool                         m_lineNumbersVisible  = true;
};

} // namespace neurus
