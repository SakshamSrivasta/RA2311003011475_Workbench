// Thin fetch wrapper. Every backend error comes back as { error, code } and
// is thrown as an ApiError whose message is ready to show to the user.

export class ApiError extends Error {
  constructor(message, code, status) {
    super(message);
    this.code = code;
    this.status = status;
  }
}

async function request(method, path, body, headers = {}) {
  let res;
  try {
    res = await fetch(path, { method, body, headers });
  } catch {
    throw new ApiError('Cannot reach the workbench backend. Is the Launch.exe window still open?', 'Offline', 0);
  }
  const type = res.headers.get('content-type') || '';
  if (!res.ok) {
    if (type.includes('application/json')) {
      const err = await res.json();
      throw new ApiError(err.error || `Request failed (${res.status})`, err.code, res.status);
    }
    throw new ApiError(`Request failed with HTTP ${res.status}.`, 'Http', res.status);
  }
  return type.includes('application/json') ? res.json() : res.text();
}

export const api = {
  get: (path) => request('GET', path),
  post: (path, json = {}) => request('POST', path, JSON.stringify(json), { 'Content-Type': 'application/json' }),
  // Sends a File straight from disk; the browser streams it, JS never reads it.
  upload: (path, file) =>
    request('POST', path, file, { 'Content-Type': 'text/plain', 'X-File-Name': encodeURIComponent(file.name) }),
};

// Fetches CSV text produced by the C++ backend and saves it via a temporary link.
export async function downloadCsv(method, path, json, filename) {
  const text = method === 'GET' ? await api.get(path) : await api.post(path, json);
  const url = URL.createObjectURL(new Blob([text], { type: 'text/csv' }));
  const a = Object.assign(document.createElement('a'), { href: url, download: filename });
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
