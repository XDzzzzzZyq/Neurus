#include "CodeEditor.h"
#include "ShaderHighlighter.h"

#include <QPainter>
#include <QResizeEvent>
#include <QTextBlock>

namespace neurus {

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent)
    , m_highlighter(std::make_unique<ShaderHighlighter>(document()))
{
    m_lineNumberArea = new LineNumberArea(this);

    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest, this, [this](const QRect& rect, int dy) {
        if (dy)
            m_lineNumberArea->scroll(0, dy);
        else
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
        if (rect.contains(viewport()->rect()))
            updateLineNumberAreaWidth();
    });
    connect(this, &CodeEditor::textChanged, this, [this]() {
        emit codeChanged(getCode());
    });

    updateLineNumberAreaWidth();
    setLineNumbersVisible(true);

    QFont font("Consolas", 10);
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
}

void CodeEditor::setLanguage(Language lang)
{
    m_highlighter->setLanguage(lang);
}

void CodeEditor::setReadOnly(bool readOnly)
{
    QPlainTextEdit::setReadOnly(readOnly);
}

void CodeEditor::setCode(const std::string& code)
{
    QString qcode = QString::fromStdString(code);
    if (toPlainText() == qcode) return;
    setPlainText(qcode);
}

std::string CodeEditor::getCode() const
{
    return toPlainText().toStdString();
}

void CodeEditor::setLineNumbersVisible(bool visible)
{
    m_lineNumbersVisible = visible;
    m_lineNumberArea->setVisible(visible);
    updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

int CodeEditor::lineNumberAreaWidth() const
{
    if (!m_lineNumbersVisible) return 0;
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    int space = 6 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void CodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), QColor(240, 240, 240));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setPen(QColor(160, 160, 160));

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.drawText(0, top, m_lineNumberArea->width() - 3, fontMetrics().height(),
                             Qt::AlignRight, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

} // namespace neurus
