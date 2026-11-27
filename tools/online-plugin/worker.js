/**
 * NX Online Plugin v1 — Reference Cloudflare Worker & Adapter Template
 *
 * Implements:
 *   GET /manifest.json
 *   GET /streams?balancer=...&tmdb_id=...&imdb_id=...&kp_id=...&type=...&title=...&original_title=...&year=...&season=...&episode=...
 *   GET /resolve?url=...
 *   GET /proxy?u=... (optional CORS + Referer/User-Agent stream proxy)
 *
 * Optional auth: set env.PLUGIN_TOKEN (Cloudflare secret or env var) to require
 * `?token=...` or `Authorization: Bearer ...`.
 */

const CORS_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, Authorization",
};

function jsonResponse(data, status = 200) {
  return new Response(JSON.stringify(data, null, 2), {
    status,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      ...CORS_HEADERS,
    },
  });
}

function checkAuth(request, url, env) {
  const required = (env && env.PLUGIN_TOKEN) ? String(env.PLUGIN_TOKEN).trim() : "";
  if (!required) return true;
  const qTok = url.searchParams.get("token") || "";
  const auth = request.headers.get("Authorization") || "";
  const bTok = auth.startsWith("Bearer ") ? auth.slice(7).trim() : "";
  return qTok === required || bTok === required;
}

// ---------------------------------------------------------------------------
// Demo provider (public Blender Open Movies HLS streams — safe for testing)
// ---------------------------------------------------------------------------
async function demoStreamsProvider(ctx) {
  const title = (ctx.title + " " + ctx.originalTitle).toLowerCase();
  const isSeries = ctx.type === "series";

  if (isSeries) {
    // Example series response showing multi-season / episode support + subtitles
    const s = ctx.season || 1;
    return {
      type: "episodes",
      voices: [
        { name: "Original (Demo)", active: true },
      ],
      seasons: [
        { number: 1, name: "Season 1", episodeCount: 2, active: s === 1 },
      ],
      items: [
        {
          title: "Big Buck Bunny (Demo Ep 1)",
          season: 1,
          episode: 1,
          voice: "Original (Demo)",
          method: "play",
          url: "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8",
          qualities: {
            "1080p": "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8",
          },
        },
        {
          title: "Tears of Steel (Demo Ep 2)",
          season: 1,
          episode: 2,
          voice: "Original (Demo)",
          method: "call",
          url: `${ctx.origin}/resolve?target=tos&token=${encodeURIComponent(ctx.token)}`,
        },
      ],
    };
  }

  // Example movie response
  const isSintel = title.includes("sintel") || ctx.tmdbId === "45745";
  const hlsUrl = isSintel
    ? "https://bitdash-a.akamaihd.net/content/sintel/hls/playlist.m3u8"
    : "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8";

  return {
    type: "movie",
    items: [
      {
        title: (ctx.title || "Demo Stream") + " (HLS)",
        voice: "Original (Demo)",
        method: "play",
        url: hlsUrl,
        qualities: {
          "1080p": hlsUrl,
          "720p": hlsUrl,
        },
      },
    ],
  };
}

// ---------------------------------------------------------------------------
// Custom provider template — add your own JSON/API/media-server adapter here!
// ---------------------------------------------------------------------------
async function customSourceProvider(ctx) {
  // Example: query your own API / Jellyfin / Plex / custom scraper:
  // const resp = await fetch(`https://your-backend.example/search?q=${encodeURIComponent(ctx.title)}`);
  // const data = await resp.json();
  // return { type: "movie", items: [ ... ] };
  return {
    type: ctx.type === "series" ? "episodes" : "movie",
    items: [],
  };
}

const PROVIDERS = {
  demo: demoStreamsProvider,
  custom: customSourceProvider,
};

export default {
  async fetch(request, env) {
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS_HEADERS });
    }

    const url = new URL(request.url);
    if (!checkAuth(request, url, env)) {
      return jsonResponse({ error: "Unauthorized" }, 401);
    }

    const pathname = url.pathname;
    const origin = url.origin;
    const token = url.searchParams.get("token") || "";

    if (pathname === "/" || pathname === "/manifest.json") {
      return jsonResponse({
        schema: "nx-online/v1",
        id: "nx-demo-plugin",
        name: "NX Demo Online",
        version: "1.0.0",
        description: "Reference NX Online Plugin v1 (Cloudflare Worker / Node.js)",
        balancers: [
          { id: "demo", name: "Demo HLS" },
          { id: "custom", name: "Custom Template" },
        ],
        endpoints: {
          streams: "/streams",
          resolve: "/resolve",
        },
      });
    }

    if (pathname === "/streams") {
      const balancer = url.searchParams.get("balancer") || "demo";
      const fn = PROVIDERS[balancer];
      if (!fn) return jsonResponse({ error: "Unknown balancer" }, 404);

      const ctx = {
        origin,
        token,
        balancer,
        tmdbId: url.searchParams.get("tmdb_id") || "",
        imdbId: url.searchParams.get("imdb_id") || "",
        kpId: url.searchParams.get("kp_id") || "",
        type: url.searchParams.get("type") || "movie",
        title: url.searchParams.get("title") || "",
        originalTitle: url.searchParams.get("original_title") || "",
        year: url.searchParams.get("year") || "",
        season: Number(url.searchParams.get("season") || "0") || 0,
        episode: Number(url.searchParams.get("episode") || "0") || 0,
      };
      const result = await fn(ctx, env);
      return jsonResponse(result);
    }

    if (pathname === "/resolve") {
      const target = url.searchParams.get("target") || "";
      if (target === "tos") {
        const stream = "https://demo.unified-streaming.com/k8s/features/stable/video/tears-of-steel/tears-of-steel.ism/.m3u8";
        return jsonResponse({
          url: stream,
          qualities: {
            "1080p": stream,
          },
        });
      }
      return jsonResponse({ error: "Unknown resolve target" }, 404);
    }

    return jsonResponse({ error: "Not Found" }, 404);
  },
};
