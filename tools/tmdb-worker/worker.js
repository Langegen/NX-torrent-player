/**
 * NX-torrent-player: Cloudflare Worker Reverse Proxy for TMDB (API & Images)
 *
 * Provides fast, reliable access to The Movie Database (TMDB) from restricted
 * regions (e.g. Russia, where TMDB GeoDNS resolves to 127.0.0.1).
 *
 * Features:
 * - Proxies /3/* to https://api.themoviedb.org/3/*
 * - Proxies /t/p/* to https://image.tmdb.org/t/p/*
 * - Transparently injects a default API key if the client does not provide one
 * - High-speed Cloudflare Edge caching for posters (7 days) and catalog lists (1 hour)
 * - Full CORS support for cross-origin and web requests
 */

const DEFAULT_API_KEY = "8cf43ad9c085135b9479ad5cf6bbcbda";

const CORS_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, HEAD, POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, Authorization, X-API-Key",
};

export default {
  async fetch(request, env, ctx) {
    // Handle CORS preflight
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS_HEADERS });
    }

    const url = new URL(request.url);
    const pathname = url.pathname;

    // Health check & welcome status
    if (pathname === "/" || pathname === "/health") {
      return new Response(
        JSON.stringify(
          {
            status: "ok",
            service: "NX-torrent-player TMDB Proxy",
            version: "1.0.0",
            endpoints: {
              api: "/3/{endpoint}",
              images: "/t/p/{size}/{path}",
            },
            timestamp: new Date().toISOString(),
          },
          null,
          2
        ),
        {
          status: 200,
          headers: {
            "Content-Type": "application/json; charset=utf-8",
            ...CORS_HEADERS,
          },
        }
      );
    }

    // 1. Poster & backdrop images: /t/p/*
    if (pathname.startsWith("/t/p/")) {
      const targetUrl = "https://image.tmdb.org" + pathname + url.search;
      try {
        const imageResponse = await fetch(targetUrl, {
          headers: {
            "User-Agent": "NX-torrent-player",
            Accept: "image/*",
          },
          cf: {
            cacheEverything: true,
            cacheTtl: 604800, // 7 days edge cache
          },
        });

        const headers = new Headers(imageResponse.headers);
        Object.entries(CORS_HEADERS).forEach(([k, v]) => headers.set(k, v));
        headers.set("Cache-Control", "public, max-age=604800, immutable");

        return new Response(imageResponse.body, {
          status: imageResponse.status,
          statusText: imageResponse.statusText,
          headers: headers,
        });
      } catch (err) {
        return new Response(JSON.stringify({ error: "Failed to fetch image: " + err.message }), {
          status: 502,
          headers: { "Content-Type": "application/json", ...CORS_HEADERS },
        });
      }
    }

    // 2. TMDB API v3 endpoints: /3/*
    if (pathname.startsWith("/3/")) {
      const targetUrl = new URL("https://api.themoviedb.org" + pathname);

      // Preserve existing query params
      for (const [k, v] of url.searchParams.entries()) {
        targetUrl.searchParams.set(k, v);
      }

      // Check if client provided authentication
      const hasAuthHeader = request.headers.has("Authorization");
      const hasKeyParam =
        targetUrl.searchParams.has("api_key") &&
        targetUrl.searchParams.get("api_key").trim() !== "";

      // If no key was supplied, inject default API key
      if (!hasAuthHeader && !hasKeyParam) {
        const activeKey = (env && env.TMDB_API_KEY) ? env.TMDB_API_KEY : DEFAULT_API_KEY;
        targetUrl.searchParams.set("api_key", activeKey);
      }

      const reqHeaders = new Headers(request.headers);
      reqHeaders.set("User-Agent", "NX-torrent-player");
      reqHeaders.delete("Host");

      // Caching TTL: lists/trending 1h, details 24h, search 15min
      let cacheTtl = 3600;
      if (pathname.includes("/movie/") || pathname.includes("/tv/")) {
        cacheTtl = 86400;
      } else if (pathname.includes("/search/")) {
        cacheTtl = 900;
      }

      try {
        const apiResponse = await fetch(targetUrl.toString(), {
          method: request.method,
          headers: reqHeaders,
          body: request.method !== "GET" && request.method !== "HEAD" ? request.body : null,
          cf: {
            cacheEverything: true,
            cacheTtl: cacheTtl,
          },
        });

        const respHeaders = new Headers(apiResponse.headers);
        Object.entries(CORS_HEADERS).forEach(([k, v]) => respHeaders.set(k, v));
        respHeaders.set("Cache-Control", `public, max-age=${cacheTtl}`);

        return new Response(apiResponse.body, {
          status: apiResponse.status,
          statusText: apiResponse.statusText,
          headers: respHeaders,
        });
      } catch (err) {
        return new Response(
          JSON.stringify({
            status_code: 502,
            status_message: "TMDB upstream connection error: " + err.message,
            success: false,
          }),
          {
            status: 502,
            headers: { "Content-Type": "application/json", ...CORS_HEADERS },
          }
        );
      }
    }

    // Default 404 for unknown routes
    return new Response(JSON.stringify({ error: "Not Found", path: pathname }), {
      status: 404,
      headers: { "Content-Type": "application/json", ...CORS_HEADERS },
    });
  },
};
