export type ErrorCodeMap = {
  '-100': string;
  '-101': string;
  '-102': string;
  '-103': string;
  '-104': string;
  '-105': string;
  '-106': string;
  '-107': string;
  '-108': string;
  '-109': string;
  // ... 其他错误码
};

export const errorCode: ErrorCodeMap = {
    '-100': '下单失败',
    '-101': '每日订单超限',
    '-102': '每秒撤单超限',
    '-103': '每秒下单超限',
    '-104': '设置每日订单上限失败',
    '-105': '设置每秒撤单限额失败',
    '-106': '设置每秒下单限额失败',
    '-107': '无效的证券代码',
    '-108': '请求中缺少type参数',
    '-109': '当前暂停报单',
}

// export default errorCode