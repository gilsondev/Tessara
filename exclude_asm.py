Import("env")

# Helium blend is ARM assembly. The preprocessor still runs on Xtensa and
# misses the ESP-IDF headers, so keep it out of this ESP32-S3 build.
env.AddBuildMiddleware(lambda node: None, "*lv_blend_helium.S*")
env.AddBuildMiddleware(lambda node: None, "*lv_blend_neon.S*")
