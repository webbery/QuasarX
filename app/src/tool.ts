const padZero = (num: Number) => {
  return num.toString().padStart(2, '0');
};

export const formatDate = (date: Date) => {
    const year = date.getFullYear();
    const month = padZero(date.getMonth() + 1); // 月份从 0 开始，需 +1
    const day = padZero(date.getDate());
    const hours = padZero(date.getHours());
    const minutes = padZero(date.getMinutes());
    const seconds = padZero(date.getSeconds());
    return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
};