#define NOMINMAX
#include <Windows.h>
#include "resource.h"

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <optional>

static unsigned int WIDTH = 2000;
static unsigned int HEIGHT = 1200;
static const sf::Color BACKGROUND_COLOR(0, 0, 0);
static const unsigned int FPS = 60;
static float SCALE = 20.0f;
static constexpr float PI = 3.14159265358979323846f;

static const std::vector<std::string> WORDS = {"love you", "Love You", "LOVE YOU"};
static const std::string CENTER_TEXT = "Love you ";
static const std::vector<sf::Color> COLORS = {
    sf::Color(255, 181, 193), // baby pink
    sf::Color(255, 192, 203), // pink
    sf::Color(250, 160, 176), // deeper baby pink
    sf::Color(255, 210, 220), // pale baby pink
    sf::Color(245, 169, 191)  // rose pink
};

static std::mt19937 rng(std::random_device{}());

static float randUniform(float lo, float hi) {
    std::uniform_real_distribution<float> d(lo, hi);
    return d(rng);
}

static int randInt(int lo, int hi) {
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng);
}

template <typename T>
static const T& randChoice(const std::vector<T>& v) {
    return v[randInt(0, (int)v.size() - 1)];

}

enum class Kind { Outline, Fill };

struct Particle {
    float x = 0.f, y = 0.f;
    int order = 0;
    Kind kind = Kind::Outline;
    std::string word;
    sf::Color color;
    int alpha = 0;
    float flicker = 0.f;
    int delay = 0;
    float size_mult = 1.f;
    sf::Texture baseTexture; 
};

static std::pair<float, float> heartXY(float t) {
    float x = 16.0f * std::pow(std::sin(t), 3.0f);
    float y = 13.0f * std::cos(t) - 5.0f * std::cos(2 * t) - 2.0f * std::cos(3 * t) - std::cos(4 * t);
    return {x, -y};
}

static std::pair<float, float> toScreen(float x, float y) {
    return {x * SCALE + WIDTH / 2.0f, y * SCALE + HEIGHT / 2.0f};
}

static Particle makeParticle(float x, float y, int order, Kind kind) {
    Particle p;
    p.x = x;
    p.y = y;
    p.order = order;
    p.kind = kind;
    p.word = randChoice(WORDS);
    p.color = randChoice(COLORS);
    p.alpha = 0;
    p.flicker = randUniform(0.0f, 2.0f * PI);
    p.delay = 0;
    p.size_mult = randUniform(0.85f, 1.15f);
    return p;
}

static std::vector<Particle> buildOutlineParticles(int n_outline, float min_gap = 30.0f) {
    std::vector<Particle> particles;
    std::vector<std::pair<float, float>> placed;
    for (int i = 0; i < n_outline; ++i) {
        float t = (static_cast<float>(i) / n_outline) * 2.0f * PI;
        auto [bx, by] = heartXY(t);
        auto [sx, sy] = toScreen(bx, by);
        bool tooClose = std::any_of(placed.begin(), placed.end(), [&](const auto& pt) {
            return std::hypot(sx - pt.first, sy - pt.second) < min_gap;
        });
        if (tooClose) continue;
        placed.push_back({sx, sy});
        particles.push_back(makeParticle(sx, sy, i, Kind::Outline));
    }
    return particles;
}

static std::vector<Particle> buildFillParticles(int n_fill, float min_gap = 46.0f) {
    std::vector<Particle> particles;
    std::vector<std::pair<float, float>> placed;
    int attempts = 0;
    int max_attempts = n_fill * 80;

    while (static_cast<int>(particles.size()) < n_fill && attempts < max_attempts) {
        attempts++;
        float t = randUniform(0.0f, 2.0f * PI);
        float r = randUniform(0.0f, 0.86f);
        auto [bx, by] = heartXY(t);
        float px = bx * r, py = by * r;
        auto [sx, sy] = toScreen(px, py);

        bool tooClose = std::any_of(placed.begin(), placed.end(), [&](const auto& pt) {
            return std::hypot(sx - pt.first, sy - pt.second) < min_gap;
        });
        if (tooClose) continue;

        placed.push_back({sx, sy});
        particles.push_back(makeParticle(sx, sy, randInt(0, 320), Kind::Fill));
    }
    return particles;
}


static sf::Texture renderWordTexture(const sf::Font& font, const std::string& word, unsigned int charSize, sf::Color color) {
    sf::Text text(font, word, charSize);
    text.setStyle(sf::Text::Bold);
    text.setFillColor(color);

    sf::FloatRect bounds = text.getLocalBounds();
    unsigned int w = std::max(1u, static_cast<unsigned int>(std::ceil(bounds.size.x + bounds.position.x * 2 + 4)));
    unsigned int h = std::max(1u, static_cast<unsigned int>(std::ceil(bounds.size.y + bounds.position.y * 2 + 4)));

    sf::RenderTexture rt(sf::Vector2u(w, h));
    rt.clear(sf::Color(0, 0, 0, 0));
    text.setPosition({2.0f - bounds.position.x, 2.0f - bounds.position.y});
    rt.draw(text);
    rt.display();

    sf::Texture tex = rt.getTexture();
    tex.setSmooth(true);
    return tex;
}

static sf::Texture renderCenterTextureWithHeart(const sf::Font& font, const std::string& text,
                                                 unsigned int charSize, sf::Color fillColor, sf::Color outlineColor,
                                                 float outlineThickness, float heartPixelHeight) {
    sf::Text sfText(font, text, charSize);
    sfText.setStyle(sf::Text::Bold);
    sfText.setFillColor(fillColor);
    sfText.setOutlineColor(outlineColor);
    sfText.setOutlineThickness(outlineThickness);
    sf::FloatRect tb = sfText.getLocalBounds();

    constexpr int N = 200;
    std::vector<sf::Vector2f> curvePts(N);
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < N; ++i) {
        float t = (static_cast<float>(i) / N) * 2.0f * PI;
        auto [hx, hy] = heartXY(t);
        curvePts[i] = {hx, hy};
        minX = std::min(minX, hx);
        maxX = std::max(maxX, hx);
        minY = std::min(minY, hy);
        maxY = std::max(maxY, hy);
    }
    float naturalH = maxY - minY;
    float naturalW = maxX - minX;
    float scale = heartPixelHeight / naturalH;
    float heartW = naturalW * scale;
    float curveMidX = (minX + maxX) / 2.0f;
    float curveMidY = (minY + maxY) / 2.0f;

    // Padding must fit both the text outline and the heart's dark border ring.
    const float pad = outlineThickness + 4.0f;
    const float gap = heartPixelHeight * 0.26f;
    unsigned int textW = std::max(1u, static_cast<unsigned int>(std::ceil(tb.size.x + tb.position.x * 2 + outlineThickness * 2)));
    unsigned int textH = std::max(1u, static_cast<unsigned int>(std::ceil(tb.size.y + tb.position.y * 2 + outlineThickness * 2)));
    unsigned int totalW = static_cast<unsigned int>(std::ceil(pad * 2 + textW + gap + heartW));
    unsigned int totalH = static_cast<unsigned int>(std::ceil(std::max(static_cast<float>(textH), heartPixelHeight) + pad * 2));

    sf::RenderTexture rt(sf::Vector2u(std::max(1u, totalW), std::max(1u, totalH)));
    rt.clear(sf::Color(0, 0, 0, 0));

    float textY = pad + (static_cast<float>(totalH) - pad * 2 - textH) / 2.0f;
    sfText.setPosition({pad - tb.position.x, textY - tb.position.y});
    rt.draw(sfText);

    float heartCenterX = pad + textW + gap + heartW / 2.0f;
    float heartCenterY = static_cast<float>(totalH) / 2.0f;

    auto toPixel = [&](sf::Vector2f p) {
        return sf::Vector2f{(p.x - curveMidX) * scale + heartCenterX, (p.y - curveMidY) * scale + heartCenterY};
    };


    std::vector<sf::Vector2f> normals(N);
    sf::Vector2f centroid{curveMidX, curveMidY};
    for (int i = 0; i < N; ++i) {
        int prev = (i - 1 + N) % N;
        int next = (i + 1) % N;
        sf::Vector2f tangent = curvePts[next] - curvePts[prev];
        sf::Vector2f normal{-tangent.y, tangent.x};
        float len = std::sqrt(normal.x * normal.x + normal.y * normal.y);
        if (len > 1e-6f) normal /= len;
        sf::Vector2f toCenter = centroid - curvePts[i];
        if (normal.x * toCenter.x + normal.y * toCenter.y < 0.0f) normal = -normal;
        normals[i] = normal;
    }

    auto buildRing = [&](float halfWidthCurveUnits, sf::Color col) {
        sf::VertexArray ring(sf::PrimitiveType::TriangleStrip, static_cast<std::size_t>(2 * (N + 1)));
        for (int i = 0; i <= N; ++i) {
            int cur = i % N;
            sf::Vector2f outerPt = curvePts[cur] - normals[cur] * halfWidthCurveUnits;
            sf::Vector2f innerPt = curvePts[cur] + normals[cur] * halfWidthCurveUnits;
            ring[2 * i].position = toPixel(outerPt);
            ring[2 * i].color = col;
            ring[2 * i + 1].position = toPixel(innerPt);
            ring[2 * i + 1].color = col;
        }
        return ring;
    };

    float strokeWidthPx = std::max(3.0f, heartPixelHeight * 0.12f);
    float halfStrokeCurveUnits = (strokeWidthPx / scale) / 2.0f;
    float outlineCurveUnits = outlineThickness / scale;

    rt.draw(buildRing(halfStrokeCurveUnits + outlineCurveUnits, outlineColor));
    rt.draw(buildRing(halfStrokeCurveUnits, fillColor));

    rt.display();

    sf::Texture tex = rt.getTexture();
    tex.setSmooth(true);
    return tex;
}

static void drawGlowText(sf::RenderTexture& glowLayer, sf::RenderTexture& textLayer,
                          const sf::Texture& tex, float x, float y, int alpha) {
    if (alpha <= 0) return;

    sf::Vector2u size = tex.getSize();

    if (alpha > 10) {
        sf::Sprite big(tex);
        big.setOrigin({size.x / 2.0f, size.y / 2.0f});
        big.setScale({2.4f, 2.4f});
        big.setPosition({x, y});
        big.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(std::max(0, alpha / 7))));
        glowLayer.draw(big);

        sf::Sprite glowSmall(tex);
        glowSmall.setOrigin({size.x / 2.0f, size.y / 2.0f});
        glowSmall.setScale({1.6f, 1.6f});
        glowSmall.setPosition({x, y});
        glowSmall.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(std::max(0, alpha / 3))));
        glowLayer.draw(glowSmall);
    }

    sf::Sprite spr(tex);
    spr.setOrigin({size.x / 2.0f, size.y / 2.0f});
    spr.setPosition({x, y});
    spr.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(alpha)));
    textLayer.draw(spr);
}

int main() {
    sf::Font fontArial;
    sf::Font fontGeorgia;
    if (!fontArial.openFromFile("C:\\Windows\\Fonts\\arial.ttf")) {
        std::cerr << "ERROR: could not load arial.ttf\n";
        return 1;
    }
    if (!fontGeorgia.openFromFile("C:\\Windows\\Fonts\\georgia.ttf")) {
        std::cerr << "ERROR: could not load georgia.ttf\n";
        return 1;
    }

    sf::Music music;
    bool hasMusic = false;
    HRSRC hAudioRes = FindResource(nullptr, MAKEINTRESOURCE(IDR_LOVE_YOU_MP3), RT_RCDATA);
    if (hAudioRes) {
        HGLOBAL hAudioData = LoadResource(nullptr, hAudioRes);
        if (hAudioData) {
            const void* audioPtr = LockResource(hAudioData);
            DWORD audioSize = SizeofResource(nullptr, hAudioRes);
            if (audioPtr && audioSize > 0) {
                hasMusic = music.openFromMemory(audioPtr, audioSize);
            }
        }
    }
    if (!hasMusic) {
        std::cerr << "WARNING: could not load embedded love_you.mp3 resource, continuing without audio\n";
    } else {
        music.play();
    }

    sf::VideoMode desktopMode = sf::VideoMode::getDesktopMode();
    WIDTH = desktopMode.size.x;
    HEIGHT = desktopMode.size.y;

    SCALE = HEIGHT / 38.0f;

    float uiScale = SCALE / 20.0f;

    sf::RenderWindow window(desktopMode, "I love you <3", sf::Style::None, sf::State::Windowed);
    window.setPosition({0, 0});

    window.requestFocus();
    window.setFramerateLimit(FPS);

    HRSRC hIconRes = FindResource(nullptr, MAKEINTRESOURCE(IDR_ICON_PNG), RT_RCDATA);
    if (hIconRes) {
        HGLOBAL hIconData = LoadResource(nullptr, hIconRes);
        if (hIconData) {
            const void* iconPtr = LockResource(hIconData);
            DWORD iconSize = SizeofResource(nullptr, hIconRes);
            sf::Image iconImage;
            if (iconPtr && iconSize > 0 && iconImage.loadFromMemory(iconPtr, iconSize)) {
                window.setIcon(iconImage.getSize(), iconImage.getPixelsPtr());
            }
        }
    }

    std::vector<Particle> outline = buildOutlineParticles(160, 30.0f * uiScale);
    std::vector<Particle> fill = buildFillParticles(130, 46.0f * uiScale);

    int outline_span = 0;
    for (auto& p : outline) outline_span = std::max(outline_span, p.order);
    const float frames_per_step = 1.6f;
    int fill_start_frame = static_cast<int>(outline_span * frames_per_step) + 30;

    for (auto& p : fill) p.delay = fill_start_frame + p.order;
    for (auto& p : outline) p.delay = static_cast<int>(p.order * frames_per_step);

    std::vector<Particle> particles;
    particles.reserve(outline.size() + fill.size());
    for (auto& p : outline) particles.push_back(std::move(p));
    for (auto& p : fill) particles.push_back(std::move(p));

    for (auto& p : particles) {
        unsigned int baseSize = (p.kind == Kind::Outline) ? 20u : 17u;
        unsigned int charSize = static_cast<unsigned int>(std::round(baseSize * uiScale * p.size_mult));
        p.baseTexture = renderWordTexture(fontArial, p.word, charSize, p.color);
    }

    unsigned int centerCharSize = static_cast<unsigned int>(std::round(54 * uiScale));
    sf::Texture centerTexture = renderCenterTextureWithHeart(fontGeorgia, CENTER_TEXT, centerCharSize,
                                                              sf::Color(255, 245, 248), sf::Color(90, 15, 40),
                                                              3.0f * uiScale, 38.0f * uiScale);
    sf::Vector2u centerSize = centerTexture.getSize();

    sf::RenderTexture glowLayer(sf::Vector2u(WIDTH, HEIGHT));
    sf::RenderTexture textLayer(sf::Vector2u(WIDTH, HEIGHT));

    int frame = 0;
    bool running = true;
    while (running && window.isOpen()) {
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                running = false;
            } else if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                if (key->code == sf::Keyboard::Key::Escape) {
                    running = false;
                }
            }
        }

        if (hasMusic && music.getStatus() == sf::SoundSource::Status::Stopped) {
            running = false;
        }

        glowLayer.clear(sf::Color(0, 0, 0, 0));
        textLayer.clear(sf::Color(0, 0, 0, 0));
        frame++;

        for (auto& p : particles) {
            if (frame > p.delay && p.alpha < 255) {
                p.alpha = std::min(255, p.alpha + 14 + randInt(0, 4));
            }

            float flick;
            if (p.alpha >= 255) {
                flick = 0.75f + 0.25f * std::sin(frame * 0.04f + p.flicker);
            } else {
                flick = 1.0f;
            }

            int alpha = static_cast<int>(p.alpha * flick);
            if (alpha <= 0) continue;

            drawGlowText(glowLayer, textLayer, p.baseTexture, p.x, p.y, alpha);
        }

        glowLayer.display();
        textLayer.display();

        window.clear(BACKGROUND_COLOR);
        sf::Sprite glowSprite(glowLayer.getTexture());
        sf::Sprite textSprite(textLayer.getTexture());
        window.draw(glowSprite);
        window.draw(textSprite);

        int center_start = fill_start_frame + 200;
        if (frame > center_start) {
            float progress = std::min(1.0f, (frame - center_start) / 60.0f);
            int center_alpha = static_cast<int>(255 * (1 - std::exp(-progress * 8)));

            float pulse = 1.0f + 0.025f * std::sin(frame * 0.05f);

            if (center_alpha > 10) {
                sf::Sprite glowCenter(centerTexture);
                glowCenter.setOrigin({centerSize.x / 2.0f, centerSize.y / 2.0f});
                glowCenter.setScale({pulse * 1.4f, pulse * 1.4f});
                glowCenter.setPosition({WIDTH / 2.0f, HEIGHT / 2.0f});
                glowCenter.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(center_alpha / 5)));
                window.draw(glowCenter);
            }

            sf::Sprite centerSprite(centerTexture);
            centerSprite.setOrigin({centerSize.x / 2.0f, centerSize.y / 2.0f});
            centerSprite.setScale({pulse, pulse});
            centerSprite.setPosition({WIDTH / 2.0f, HEIGHT / 2.0f});
            centerSprite.setColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(std::clamp(center_alpha, 0, 255))));
            window.draw(centerSprite);
        }

        window.display();
    }

    return 0;
}
