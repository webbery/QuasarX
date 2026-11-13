/**
 * Markdown 渲染单例
 *
 * 支持代码高亮（hljs）与 LaTeX（KaTeX）。
 * 放在 lib 下供 ChatBox / Agent 面板共用，避免重复注册。
 */

import MarkdownIt from 'markdown-it'
import hljs from 'highlight.js/lib/core'
import python from 'highlight.js/lib/languages/python'
import json from 'highlight.js/lib/languages/json'
import javascript from 'highlight.js/lib/languages/javascript'
import typescript from 'highlight.js/lib/languages/typescript'
import texmath from 'markdown-it-texmath'
import katex from 'katex'
import 'katex/dist/katex.min.css'

let registered = false

function ensureLanguages() {
  if (registered) return
  registered = true
  hljs.registerLanguage('python', python)
  hljs.registerLanguage('json', json)
  hljs.registerLanguage('javascript', javascript)
  hljs.registerLanguage('typescript', typescript)
}

ensureLanguages()

export const md = new MarkdownIt({
  html: true,
  breaks: true,
  linkify: true,
  highlight: (str: string, lang: string) => {
    if (lang && hljs.getLanguage(lang)) {
      try {
        return `<pre class="hljs"><code class="hljs language-${lang}">${hljs.highlight(str, { language: lang }).value}</code></pre>`
      } catch {
        /* 忽略高亮失败，走默认转义 */
      }
    }
    return ''
  },
})

md.use(texmath, {
  engine: katex,
  delimiters: ['dollars', 'brackets', 'kramdown', 'doxygen'],
  outerOpen: '$',
  outerClose: '$',
})

export function renderMarkdown(text: string): string {
  return md.render(text || '')
}
