/**
 * PDF 解析工具
 * 使用 pdf-parse-new 库解析 PDF 文本内容
 */

export interface PdfParseResult {
  text: string;        // 完整文本
  pages: number;       // 页数
  title: string;       // 提取的标题
  chunks: TextChunk[]; // 分块后的文本
  summary: string;     // 摘要
}

export interface TextChunk {
  index: number;
  content: string;
}

/**
 * PDF 解析配置
 */
interface ParseConfig {
  maxChunkSize?: number;    // 最大块大小（字符数），默认 800
  minChunkSize?: number;    // 最小块大小（字符数），默认 100
  shortChunkThreshold?: number; // 短块阈值（字符数），小于此值不做 summary，默认 50
  summaryLength?: number;   // 摘要字符数，默认 300
}

/**
 * 从 ArrayBuffer 解析 PDF
 * @param fileData PDF 文件的 ArrayBuffer
 * @param config 解析配置
 * @returns PDF 解析结果
 */
export async function parsePdf(
  fileData: ArrayBuffer,
  config: ParseConfig = {}
): Promise<PdfParseResult> {
  const {
    maxChunkSize = 800,
    minChunkSize = 100,
    shortChunkThreshold = 50,
    summaryLength = 300,
  } = config;

  // 使用 pdf-parse-new 解析
  // 注意: pdf-parse-new 在 Electron 渲染器中需要通过主进程调用
  // 这里提供一个基于主进程的实现方案
  const text = await extractTextFromPdf(fileData);
  const pages = estimatePageCount(text);
  const title = extractTitle(text);
  const chunks = chunkByParagraph(text, maxChunkSize, minChunkSize, shortChunkThreshold);
  const summary = generateSummary(text, summaryLength);

  return {
    text,
    pages,
    title,
    chunks: chunks.map((content, index) => ({ index, content })),
    summary,
  };
}

/**
 * 从 PDF ArrayBuffer 提取文本
 * 通过 Electron IPC 调用主进程的 pdf-parse
 */
async function extractTextFromPdf(fileData: ArrayBuffer): Promise<string> {
  const { ipcRenderer } = require('electron');

  try {
    const uint8Array = new Uint8Array(fileData);
    const result = await ipcRenderer.invoke(
      'parse-pdf',
      Array.from(uint8Array)
    );
    if (result.success) {
      return result.text;
    }
    console.warn('[pdfParser] 主进程解析失败');
    return '[PDF 解析失败]';
  } catch (e) {
    console.error('[pdfParser] IPC 调用失败:', e);
    return '[PDF 解析失败]';
  }
}

/**
 * 估算页数
 */
function estimatePageCount(text: string): number {
  // 粗略估计：每页约 3000 字符
  return Math.max(1, Math.ceil(text.length / 3000));
}

/**
 * 从文本中提取标题（取第一行非空文本）
 */
function extractTitle(text: string): string {
  const lines = text.split(/\r?\n/).filter(line => line.trim().length > 0);
  if (lines.length > 0) {
    return lines[0].substring(0, 100); // 限制标题长度
  }
  return '未命名文档';
}

/**
 * 按段落分块
 * @param text 完整文本
 * @param maxChunkSize 最大块大小（字符数），默认 800
 * @param minChunkSize 最小块大小（字符数），默认 100
 * @param shortChunkThreshold 短块阈值（字符数），小于此值不做 summary
 * @returns 分块数组
 */
function chunkByParagraph(
  text: string,
  maxChunkSize: number = 800,
  minChunkSize: number = 100,
  shortChunkThreshold: number = 50
): string[] {
  // 1. 按段落分割（双换行 = 段落边界，单换行 = 句子边界）
  const paragraphs = text.split(/\n\n+/).map(p => p.trim()).filter(p => p.length > 0);

  // 降级：如果段落数太少（< 3），回退到固定字符分块
  if (paragraphs.length < 3) {
    console.warn('[pdfParser] 段落数太少（< 3），回退到固定字符分块');
    return chunkByFixedChar(text, maxChunkSize);
  }

  const chunks: string[] = [];
  let currentChunk = '';

  for (const para of paragraphs) {
    // 如果当前块 + 新段落不超过最大限制，追加
    if (currentChunk.length + para.length + 2 <= maxChunkSize) {
      currentChunk = currentChunk ? `${currentChunk}\n\n${para}` : para;
    } else {
      // 当前块已满，保存
      if (currentChunk.length >= minChunkSize) {
        chunks.push(currentChunk);
      }
      // 如果段落本身超长，需要进一步按句子拆分
      if (para.length > maxChunkSize) {
        const subChunks = splitLongParagraph(para, maxChunkSize, minChunkSize);
        chunks.push(...subChunks);
        currentChunk = '';
      } else {
        currentChunk = para;
      }
    }
  }

  // 处理最后一个块
  if (currentChunk.length >= minChunkSize) {
    chunks.push(currentChunk);
  }

  console.log(`[pdfParser] 按段落分块完成: ${paragraphs.length} 段落 → ${chunks.length} 块`);
  return chunks;
}

/**
 * 固定字符数分块（降级方案）
 */
function chunkByFixedChar(text: string, chunkSize: number): string[] {
  const chunks: string[] = [];
  const cleaned = text.replace(/\s+/g, ' ').trim();

  for (let i = 0; i < cleaned.length; i += chunkSize) {
    const chunk = cleaned.substring(i, i + chunkSize);
    if (chunk.length > 0) {
      chunks.push(chunk);
    }
  }

  return chunks;
}

/**
 * 超长段落按句子拆分（带重叠）
 */
function splitLongParagraph(
  text: string,
  maxChunkSize: number,
  minChunkSize: number
): string[] {
  // 按句子分割（句号、问号、感叹号 + 空格/换行）
  const sentences = text.split(/(?<=[。！？.!?])\s*/).filter(s => s.trim());

  const chunks: string[] = [];
  let currentChunk = '';
  let overlap = ''; // 保留最后 1-2 个句子作为重叠

  for (const sentence of sentences) {
    if (currentChunk.length + sentence.length <= maxChunkSize) {
      currentChunk += sentence;
    } else {
      if (currentChunk.length >= minChunkSize) {
        chunks.push(overlap + currentChunk);
      }
      // 重叠：保留最后 2 个句子
      const lastSentences = currentChunk.split(/(?<=[。！？.!?])\s*/).slice(-2).join('');
      overlap = lastSentences;
      currentChunk = sentence;
    }
  }

  if (currentChunk.length >= minChunkSize) {
    chunks.push(overlap + currentChunk);
  }

  return chunks;
}

/**
 * 生成摘要（截取开头 + 关键词提取）
 */
function generateSummary(text: string, maxLength: number): string {
  const cleaned = text.replace(/\s+/g, ' ').trim();

  if (cleaned.length <= maxLength) {
    return cleaned;
  }

  // 简单策略：取前 N 个字符
  let summary = cleaned.substring(0, maxLength);

  // 确保不在单词中间截断
  const lastSpace = summary.lastIndexOf(' ');
  if (lastSpace > maxLength * 0.8) {
    summary = summary.substring(0, lastSpace);
  }

  return summary + '...';
}
