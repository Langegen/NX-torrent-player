# TMDB Proxy (Cloudflare Worker) для NX-torrent-player

Серверный прокси для доступа к каталогу и постерам **The Movie Database (TMDB)** из России и других регионов с сетевыми ограничениями.

---

## Что делает этот воркер

- Перенаправляет запросы каталога `/3/*` на `https://api.themoviedb.org/3/*`.
- Перенаправляет постеры и фоны `/t/p/*` на `https://image.tmdb.org/t/p/*`.
- Автоматически подставляет рабочий ключ API TMDB, если клиент не указал свой.
- Кэширует постеры на границе Cloudflare (CDN) на 7 дней, ускоряя отрисовку в плеере.
- Возвращает необходимые CORS-заголовки и не блокирует клиент Nintendo Switch / ПК.

---

## Способ 1: Быстрый запуск через браузер (без установки программ, ~2 минуты)

1. Зарегистрируйтесь бесплатно на [Cloudflare](https://dash.cloudflare.com/) (или войдите в свой аккаунт).
2. В панели слева выберите **Workers & Pages** -> **Create application** -> **Create Worker**.
3. Укажите любое имя (например, `nx-tmdb`) и нажмите **Deploy**.
4. Нажмите **Edit code**.
5. Замените весь код в редакторе на содержимое файла [`worker.js`](worker.js).
6. Нажмите кнопку **Save and deploy**.
7. Скопируйте полученный адрес воркера (например, `https://nx-tmdb.<ваш-аккаунт>.workers.dev`).
8. Вставьте его в NX-torrent-player:
   - В приложении: **Опции -> Сеть -> TMDB: Прокси**.

---

## Способ 2: Запуск через консоль (Wrangler CLI)

1. Установите Node.js (если не установлен).
2. Откройте терминал в папке `tools/tmdb-worker`:
   ```bash
   npm install -g wrangler
   wrangler login
   wrangler deploy
   ```
3. После завершения деплоя консоль выведет ваш готовый URL:
   ```text
   https://nx-tmdb-proxy.<subdomain>.workers.dev
   ```

---

## Проверка работы

Откройте в браузере или через curl:
```bash
curl https://<ваш-воркер>.workers.dev/health
```
Воркер вернет:
```json
{
  "status": "ok",
  "service": "NX-torrent-player TMDB Proxy"
}
```
А при запросе фильма:
```bash
curl https://<ваш-воркер>.workers.dev/3/movie/550
```
Вернутся данные фильма «Бойцовский клуб» из базы TMDB!
