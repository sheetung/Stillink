// Aggregate reads are independent of anonymous event collection and USB operations.
export function createPublicCounters(endpoint, document) {
  let url;
  try {
    url = new URL(endpoint);
    if (url.protocol !== 'https:') return () => {};
    url.pathname = '/public-stats';
    url.search = ''; url.hash = '';
  } catch { return () => {}; }
  const panel = document.getElementById('publicStats');
  if (!panel) return () => {};
  panel.hidden = false;
  return async () => {
    try {
      const response = await fetch(url.href, { mode: 'cors', credentials: 'omit', cache: 'no-store' });
      if (!response.ok) return;
      const data = await response.json();
      if (![data.visitors, data.success].every(n => Number.isSafeInteger(n) && n >= 0)) return;
      document.getElementById('visitorCount').textContent = data.visitors.toLocaleString('zh-CN');
      document.getElementById('successCount').textContent = data.success.toLocaleString('zh-CN');
    } catch { /* An unavailable counter must not interrupt flashing. */ }
  };
}
