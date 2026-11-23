Import("env")

# Apply only to the ESP32 DevKit build. The upstream WebSocketsClient currently
# treats a NULL CA bundle as setInsecure(); switch that branch to the ESP32
# Arduino core's built-in Mozilla CA bundle instead.
if env.subst("$PIOENV") == "esp32_devkit":
    from pathlib import Path

    source = (
        Path(env.subst("$PROJECT_LIBDEPS_DIR"))
        / env.subst("$PIOENV")
        / "WebSockets"
        / "src"
        / "WebSocketsClient.cpp"
    )
    if not source.is_file():
        raise RuntimeError("WebSocketsClient.cpp not found; refusing an unpatched TLS build")

    text = source.read_text(encoding="utf-8")
    insecure = """            } else if(!SSL_FINGERPRINT_IS_SET) {
                _client.ssl->setInsecure();"""
    secure = """            } else if(!SSL_FINGERPRINT_IS_SET) {
                // Verify against the ESP32 Arduino core's embedded public CA bundle.
                _client.ssl->useBuiltinCACertBundle();"""

    if secure in text:
        pass
    elif insecure in text:
        source.write_text(text.replace(insecure, secure, 1), encoding="utf-8")
    else:
        raise RuntimeError(
            "Unexpected WebSocketsClient TLS code; refusing to build with unknown verification behavior"
        )
