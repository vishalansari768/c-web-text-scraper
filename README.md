# C Web Text Scraper

A fast, lightweight web text extractor built using **C** (`libcurl`, `libxml2`, and `Mongoose` HTTP server) with a modern **HTML/CSS/JS** frontend.

## Features
- Extracts clean readable text (`<h1>` to `<h3>`, `<p>`, `<li>`) from any webpage.
- Ignores `<script>`, `<style>`, `<noscript>`, and `<svg>` tags.
- Built-in local web server (`http://127.0.0.1:8080`) with live preview, copy, and `.txt` download.

## How to Compile & Run (Windows MSYS2 UCRT64)
```bash
gcc scraper.c mongoose.c -o webapp_scraper.exe $(pkg-config --cflags --libs libcurl libxml-2.0) -lws2_32
./webapp_scraper.exe
