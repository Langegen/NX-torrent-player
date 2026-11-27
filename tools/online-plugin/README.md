# NX Online Plugin v1 (Reference Template)

Готовый стартовый шаблон собственного плагина онлайн-просмотра для **NX Torrent Player**.

## Локальный запуск (Node.js 18+)

```sh
cd tools/online-plugin
node server.mjs
```

Сервер запустится на `http://0.0.0.0:8787`. В приложении перейдите в **Настройки → Онлайн → + Добавить плагин** и укажите `http://<IP_вашего_ПК>:8787`.

С авторизацией по токену:
```sh
PLUGIN_TOKEN=mysecret node server.mjs
```

## Деплой на Cloudflare Workers (бесплатно)

```sh
cd tools/online-plugin
npx wrangler deploy
```

После деплоя укажите выданный URL вида `https://nx-online-plugin.<subdomain>.workers.dev` в **Настройки → Онлайн** в приложении.

Подробная документация и спецификация протокола: [docs/ONLINE_PLUGINS.md](../../docs/ONLINE_PLUGINS.md).
