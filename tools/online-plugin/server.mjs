import http from "node:http";
import worker from "./worker.js";

const PORT = Number(process.env.PORT || 8787);
const HOST = process.env.HOST || "0.0.0.0";
const env = {
  PLUGIN_TOKEN: process.env.PLUGIN_TOKEN || "",
};

const server = http.createServer(async (req, res) => {
  try {
    const fullUrl = `http://${req.headers.host || `127.0.0.1:${PORT}`}${req.url || "/"}`;
    const headers = new Headers();
    for (const [k, v] of Object.entries(req.headers)) {
      if (typeof v === "string") headers.set(k, v);
      else if (Array.isArray(v)) headers.set(k, v.join(", "));
    }
    const request = new Request(fullUrl, {
      method: req.method || "GET",
      headers,
    });
    const response = await worker.fetch(request, env);
    res.writeHead(response.status, Object.fromEntries(response.headers.entries()));
    const buf = Buffer.from(await response.arrayBuffer());
    res.end(buf);
  } catch (err) {
    res.writeHead(500, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: String(err) }));
  }
});

server.listen(PORT, HOST, () => {
  console.log(`[nx-online-plugin] Listening on http://${HOST}:${PORT}`);
  console.log(`[nx-online-plugin] Manifest: http://127.0.0.1:${PORT}/manifest.json`);
});
