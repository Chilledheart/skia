
out vec4 sk_FragColor;
uniform vec4 colorRed;
uniform vec4 colorGreen;
vec4 main() {
    bool _0_ok = true;
    float _1_x = 34.0;
    _0_ok = _0_ok && _1_x == 34.0;
    _1_x = 30.0;
    _0_ok = _0_ok && _1_x == 30.0;
    _1_x = 64.0;
    _0_ok = _0_ok && _1_x == 64.0;
    _1_x = 16.0;
    _0_ok = _0_ok && _1_x == 16.0;
    _1_x = 19.0;
    _0_ok = _0_ok && _1_x == 19.0;
    _1_x = 1.0;
    _0_ok = _0_ok && _1_x == 1.0;
    _1_x = -2.0;
    _0_ok = _0_ok && _1_x == -2.0;
    _1_x = 3.0;
    _0_ok = _0_ok && _1_x == 3.0;
    _1_x = -4.0;
    _0_ok = _0_ok && _1_x == -4.0;
    _1_x = 5.0;
    _0_ok = _0_ok && _1_x == 5.0;
    _1_x = -6.0;
    _0_ok = _0_ok && _1_x == -6.0;
    _1_x = 7.0;
    _0_ok = _0_ok && _1_x == 7.0;
    _1_x = -8.0;
    _0_ok = _0_ok && _1_x == -8.0;
    _1_x = 9.0;
    _0_ok = _0_ok && _1_x == 9.0;
    _1_x = -10.0;
    _0_ok = _0_ok && _1_x == -10.0;
    _1_x = 11.0;
    _0_ok = _0_ok && _1_x == 11.0;
    _1_x = -12.0;
    _0_ok = _0_ok && _1_x == -12.0;
    float _2_unknown = sqrt(4.0);
    _1_x = _2_unknown + 0.0;
    _0_ok = _0_ok && _1_x == _2_unknown;
    _1_x = 0.0 + _2_unknown;
    _0_ok = _0_ok && _1_x == _2_unknown;
    _1_x = _2_unknown - 0.0;
    _0_ok = _0_ok && _1_x == _2_unknown;
    _1_x = _2_unknown * 0.0;
    _0_ok = _0_ok && _1_x == 0.0;
    _1_x = _2_unknown * 1.0;
    _0_ok = _0_ok && _1_x == _2_unknown;
    _1_x = 1.0 * _2_unknown;
    _0_ok = _0_ok && _1_x == _2_unknown;
    _1_x = 0.0 * _2_unknown;
    _0_ok = _0_ok && _1_x == 0.0;
    _1_x = _2_unknown / 1.0;
    _0_ok = _0_ok && _1_x == _2_unknown;
    _1_x = 0.0 / _2_unknown;
    _0_ok = _0_ok && _1_x == 0.0;
    _1_x += 1.0;
    _0_ok = _0_ok && _1_x == 1.0;
    _1_x += 0.0;
    _0_ok = _0_ok && _1_x == 1.0;
    _1_x -= 2.0;
    _0_ok = _0_ok && _1_x == -1.0;
    _1_x -= 0.0;
    _0_ok = _0_ok && _1_x == -1.0;
    _1_x *= 1.0;
    _0_ok = _0_ok && _1_x == -1.0;
    _1_x *= 2.0;
    _0_ok = _0_ok && _1_x == -2.0;
    _1_x /= 1.0;
    _0_ok = _0_ok && _1_x == -2.0;
    _1_x /= 2.0;
    _0_ok = _0_ok && _1_x == -1.0;
    return _0_ok ? colorGreen : colorRed;

}
