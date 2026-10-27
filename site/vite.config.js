import { defineConfig } from "vite";

// GitHub Pages project site: https://gilsondev.github.io/Tessera/ (repo name)
export default defineConfig({
  base: "/Tessera/",
  publicDir: "public",
  build: {
    outDir: "dist",
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: "index.html",
        usar: "usar.html",
      },
    },
  },
});
