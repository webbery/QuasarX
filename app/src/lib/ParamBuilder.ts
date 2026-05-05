/**
 * 参数构建器
 * 将字符串 spec（如 "quote:current"）映射为实际参数对象
 */

export interface ParamSpec {
  api: string;
  scope: string;
  parseDate?: boolean;
}

export class ParamBuilder {
  private static specMap: Map<string, ParamSpec> = new Map([
    ['quote:current',  { api: 'quote',  scope: 'current' }],
    ['quote:history',  { api: 'quote',  scope: 'history', parseDate: true }],
    ['account:risk',   { api: 'account', scope: 'risk' }],
    ['position:analysis', { api: 'position', scope: 'analysis' }],
  ]);

  static build(specKey: string, userInput: string): Record<string, any> {
    const spec = this.specMap.get(specKey);
    if (!spec) return {};

    const params: Record<string, any> = { scope: spec.scope };

    if (spec.parseDate) {
      params.date = this.extractDate(userInput);
    }

    return params;
  }

  private static extractDate(input: string): string | null {
    const patterns = [
      { regex: /今天|当前|现在/, value: 'today' },
      { regex: /昨天/, value: 'yesterday' },
      { regex: /上周/, value: 'last_week' },
    ];
    for (const p of patterns) {
      if (p.regex.test(input)) return p.value;
    }
    return null;
  }
}
