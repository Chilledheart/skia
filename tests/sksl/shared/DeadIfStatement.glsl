
out vec4 sk_FragColor;
uniform vec4 colorGreen;
uniform vec4 colorRed;
vec4 main() {
    bool x = true;
    if (x) return colorGreen;
    if (!x) return colorRed;
}
