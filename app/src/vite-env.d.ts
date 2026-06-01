/// <reference types="vite/client" />

declare module 'markdown-it-texmath' {
  import type MarkdownIt from 'markdown-it';
  interface TexmathOptions {
    engine?: any;
    delimiters?: 'brackets' | 'kramdown' | 'doxygen' | 'beg_end' | 'beg_end_single_dollar' | 'single_dollar' | 'double_backslash';
    externalRules?: any;
  }
  function texmath(md: MarkdownIt, options?: TexmathOptions): void;
  export default texmath;
}
