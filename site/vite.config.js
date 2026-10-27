import { defineConfig } from "vite";

// GitHub Pages project site: https://gilsondev.github.io/Tessara/ (repo name)
export default defineConfig({
  base: "/Tessara/",
  publicDir: "public",
  build: {
    outDir: "dist",
    emptyOutDir: true,
  },
});
