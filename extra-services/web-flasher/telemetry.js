// Anonymous, best-effort events. No awaited network calls in the flashing path.
export function createTelemetry(endpoint, onRecorded = () => {}) {
  const noop = () => () => {};
  try {
    if (!endpoint || navigator.doNotTrack === '1' || window.doNotTrack === '1') return { start: noop };
    const url = new URL(endpoint);
    if (url.protocol !== 'https:') return { start: noop };
    let visitor;
    try {
      visitor = localStorage.getItem('stillink-flasher-visitor');
      if (!/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(visitor || '')) {
        visitor = crypto.randomUUID();
        localStorage.setItem('stillink-flasher-visitor', visitor);
      }
    } catch { visitor = crypto.randomUUID(); }
    function send(event, fields = {}) {
      try {
        fetch(url.href, {
          method: 'POST', mode: 'cors', credentials: 'omit', keepalive: true,
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ event_id: crypto.randomUUID(), visitor_id: visitor, event, ...fields }),
        }).then(response => {
          if (response.ok && (event === 'visit' || event === 'flash_success')) onRecorded();
        }).catch(() => {});
      } catch { /* Analytics must never interrupt USB flashing. */ }
    }
    send('visit');
    return {
      start(kind, fields = {}) {
        try {
          const attempt_id = crypto.randomUUID(), began = Date.now();
          let finished = false;
          send(kind + '_start', { ...fields, attempt_id });
          return (result, error = '', details = {}) => {
            if (finished) return;
            finished = true;
            send(kind + '_' + result, {
              ...fields, ...details, attempt_id, error,
              duration_ms: Math.min(86400000, Math.max(0, Date.now() - began)),
            });
          };
        } catch { return () => {}; }
      },
    };
  } catch { return { start: noop }; }
}
