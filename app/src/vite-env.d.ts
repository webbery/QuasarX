/// <reference types="vite/client" />

/** Skills 知识库 IPC 返回的摘要信息（不含正文） */
interface SkillInfo {
  id: string
  name: string
  description: string
  keywords: string[]
}

/** Skills 知识库 IPC 返回的完整信息（含正文） */
interface SkillDetail extends SkillInfo {
  content: string
}

/** Skills 知识库 IPC API 形状（preload 暴露给渲染进程） */
interface SkillsAPI {
  /** 列出所有可用 Skill（仅摘要，不含正文） */
  list(): Promise<{ success: boolean; skills: SkillInfo[] }>
  /** 读取指定 Skill 的完整内容 */
  read(skillId: string): Promise<{ success: boolean; error?: string } & SkillDetail>
}

// 挂载到 window.skillsAPI
interface Window {
  skillsAPI: SkillsAPI
}

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
