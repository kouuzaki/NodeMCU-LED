#include <Arduino.h>
#include <TFT_eSPI.h>

namespace {
constexpr uint8_t TFT_BACKLIGHT_PIN = D4;
constexpr uint32_t SERIAL_TIMEOUT_MS = 3000;
constexpr size_t SERIAL_BUFFER_SIZE = 640;

TFT_eSPI tft;
char serialBuffer[SERIAL_BUFFER_SIZE];
size_t serialLength = 0;

bool spotifyConnected = false;
bool spotifyPlaying = false;
uint32_t positionMs = 0;
uint32_t durationMs = 0;
uint32_t lastMessageMs = 0;
char trackId[32] = "";
char title[96] = "";
char artist[96] = "";
char lyricStatus[16] = "loading";
char previousLyric[152] = "";
char currentLyric[152] = "";
char nextLyric[152] = "";

char renderedTrackId[32] = "";
bool renderedConnected = false;
bool renderedPlaying = false;
char renderedLyricStatus[16] = "";
char renderedCurrentLyric[152] = "";
uint32_t lastProgressDrawMs = 0;

void copyField(char* destination, size_t size, const char* source) {
  if (size == 0) return;
  strncpy(destination, source != nullptr ? source : "", size - 1);
  destination[size - 1] = '\0';
}

void formatTime(uint32_t milliseconds, char* output, size_t size) {
  const uint32_t totalSeconds = milliseconds / 1000;
  snprintf(output, size, "%lu:%02lu",
           static_cast<unsigned long>(totalSeconds / 60),
           static_cast<unsigned long>(totalSeconds % 60));
}

void drawCentered(const char* text, int32_t y, uint8_t preferredFont,
                  uint16_t color) {
  uint8_t font = preferredFont;
  while (font > 1 && tft.textWidth(text, font) > 300) {
    font = (font == 4) ? 2 : 1;
  }
  tft.setTextColor(color, TFT_NAVY);
  tft.drawString(text, 160, y, font);
}

void drawStaticScreen() {
  tft.fillScreen(TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  drawCentered("SPOTIFY LYRIC DISPLAY", 16, 2, TFT_CYAN);
  tft.drawFastHLine(15, 31, 290, TFT_DARKGREY);
}

void playBootAnimation() {
  constexpr uint16_t spotifyGreen = 0x3666;
  constexpr int16_t barCount = 7;
  constexpr int16_t barWidth = 13;
  constexpr int16_t barGap = 7;
  constexpr int16_t startX = 99;
  constexpr int16_t baselineY = 145;

  tft.fillScreen(TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("SPOTIFY", 160, 62, 4);
  tft.setTextColor(spotifyGreen, TFT_NAVY);
  tft.drawString("LYRIC DISPLAY", 160, 88, 2);

  for (uint8_t frame = 0; frame < 28; frame++) {
    tft.fillRect(88, 105, 145, 43, TFT_NAVY);
    for (int16_t bar = 0; bar < barCount; bar++) {
      const uint8_t phase = (frame * 3 + bar * 5) % 18;
      const int16_t height = 8 + (phase <= 9 ? phase * 3 : (18 - phase) * 3);
      const int16_t x = startX + bar * (barWidth + barGap);
      tft.fillRoundRect(x, baselineY - height, barWidth, height, 3,
                        spotifyGreen);
    }
    delay(42);
  }

  tft.fillRect(70, 165, 180, 24, TFT_NAVY);
  tft.setTextColor(TFT_LIGHTGREY, TFT_NAVY);
  tft.drawString("Connecting to Spotify...", 160, 177, 2);
  delay(450);
}

void drawMetadata() {
  tft.fillRect(5, 34, 310, 58, TFT_NAVY);

  if (!spotifyConnected) {
    drawCentered("Waiting for Spotify...", 53, 4, TFT_WHITE);
    drawCentered("Run spotify_bridge.py", 78, 2, TFT_LIGHTGREY);
    return;
  }

  drawCentered(title, 48, 2, TFT_YELLOW);
  drawCentered(artist, 68, 2, TFT_WHITE);
  drawCentered(spotifyPlaying ? "PLAYING" : "PAUSED", 86, 1,
               spotifyPlaying ? TFT_GREEN : TFT_ORANGE);
}

void drawWrappedCentered(const char* text, int32_t y, uint8_t font,
                         uint16_t color, uint8_t maxLines) {
  char working[152];
  char line[152] = "";
  copyField(working, sizeof(working), text);
  char* savePointer = nullptr;
  char* word = strtok_r(working, " ", &savePointer);
  uint8_t lineNumber = 0;
  const int16_t lineHeight = font == 4 ? 27 : (font == 2 ? 16 : 10);

  tft.setTextColor(color, TFT_NAVY);
  while (word != nullptr && lineNumber < maxLines) {
    char candidate[152] = "";
    copyField(candidate, sizeof(candidate), line);
    if (line[0] != '\0') {
      strncat(candidate, " ", sizeof(candidate) - strlen(candidate) - 1);
    }
    strncat(candidate, word, sizeof(candidate) - strlen(candidate) - 1);
    if (line[0] != '\0' && tft.textWidth(candidate, font) > 304) {
      tft.drawString(line, 160, y + lineNumber * lineHeight, font);
      lineNumber++;
      copyField(line, sizeof(line), word);
    } else {
      copyField(line, sizeof(line), candidate);
    }
    word = strtok_r(nullptr, " ", &savePointer);
  }
  if (line[0] != '\0' && lineNumber < maxLines) {
    tft.drawString(line, 160, y + lineNumber * lineHeight, font);
  }
}

void drawLyrics() {
  tft.fillRect(5, 94, 310, 98, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);

  if (!spotifyConnected) return;

  if (strcmp(lyricStatus, "loading") == 0) {
    drawCentered("Finding synced lyrics...", 139, 2, TFT_LIGHTGREY);
  } else if (strcmp(lyricStatus, "missing") == 0) {
    drawCentered("Synced lyrics unavailable", 139, 2, TFT_LIGHTGREY);
  } else if (strcmp(lyricStatus, "instrumental") == 0) {
    drawCentered("Instrumental", 139, 4, TFT_LIGHTGREY);
  } else if (strcmp(lyricStatus, "error") == 0) {
    drawCentered("Could not load lyrics", 139, 2, TFT_ORANGE);
  } else {
    drawWrappedCentered(previousLyric, 105, 2, TFT_DARKGREY, 1);
    drawWrappedCentered(currentLyric, 133, 2, TFT_YELLOW, 2);
    drawWrappedCentered(nextLyric, 174, 2, TFT_LIGHTGREY, 1);
  }
}

void drawProgress() {
  constexpr int32_t x = 15;
  constexpr int32_t y = 204;
  constexpr int32_t width = 290;
  constexpr int32_t height = 9;

  tft.drawRect(x, y, width, height, TFT_WHITE);
  tft.fillRect(x + 1, y + 1, width - 2, height - 2, TFT_DARKGREY);

  if (durationMs > 0) {
    const uint32_t boundedPosition = min(positionMs, durationMs);
    const int32_t filled = static_cast<int32_t>(
        (static_cast<uint64_t>(width - 2) * boundedPosition) / durationMs);
    if (filled > 0) tft.fillRect(x + 1, y + 1, filled, height - 2, TFT_GREEN);
  }

  char current[12];
  char duration[12];
  formatTime(positionMs, current, sizeof(current));
  formatTime(durationMs, duration, sizeof(duration));
  tft.fillRect(15, 218, 290, 20, TFT_NAVY);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString(current, 15, 220, 2);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(duration, 305, 220, 2);
  tft.setTextDatum(MC_DATUM);
}

void parseSerialMessage(char* line) {
  char* savePointer = nullptr;
  char* prefix = strtok_r(line, "|", &savePointer);
  if (prefix == nullptr) return;

  if (strcmp(prefix, "LYRIC") == 0) {
    char* status = strtok_r(nullptr, "|", &savePointer);
    char* previous = strtok_r(nullptr, "|", &savePointer);
    char* current = strtok_r(nullptr, "|", &savePointer);
    char* following = strtok_r(nullptr, "|", &savePointer);
    if (status == nullptr || previous == nullptr || current == nullptr ||
        following == nullptr) return;
    copyField(lyricStatus, sizeof(lyricStatus), status);
    copyField(previousLyric, sizeof(previousLyric), previous);
    copyField(currentLyric, sizeof(currentLyric), current);
    copyField(nextLyric, sizeof(nextLyric), following);
    return;
  }

  if (strcmp(prefix, "SPOT") != 0) return;

  char* connected = strtok_r(nullptr, "|", &savePointer);
  if (connected == nullptr) return;

  spotifyConnected = atoi(connected) != 0;
  lastMessageMs = millis();
  if (!spotifyConnected) return;

  char* playing = strtok_r(nullptr, "|", &savePointer);
  char* position = strtok_r(nullptr, "|", &savePointer);
  char* duration = strtok_r(nullptr, "|", &savePointer);
  char* id = strtok_r(nullptr, "|", &savePointer);
  char* trackTitle = strtok_r(nullptr, "|", &savePointer);
  char* trackArtist = strtok_r(nullptr, "|", &savePointer);
  if (playing == nullptr || position == nullptr || duration == nullptr ||
      id == nullptr || trackTitle == nullptr || trackArtist == nullptr) {
    return;
  }

  spotifyPlaying = atoi(playing) != 0;
  positionMs = strtoul(position, nullptr, 10);
  durationMs = strtoul(duration, nullptr, 10);
  if (strcmp(trackId, id) != 0) {
    copyField(lyricStatus, sizeof(lyricStatus), "loading");
    previousLyric[0] = '\0';
    currentLyric[0] = '\0';
    nextLyric[0] = '\0';
  }
  copyField(trackId, sizeof(trackId), id);
  copyField(title, sizeof(title), trackTitle);
  copyField(artist, sizeof(artist), trackArtist);
}

void readSerialMessages() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\r') continue;
    if (incoming == '\n') {
      serialBuffer[serialLength] = '\0';
      parseSerialMessage(serialBuffer);
      serialLength = 0;
      continue;
    }
    if (serialLength < SERIAL_BUFFER_SIZE - 1) {
      serialBuffer[serialLength++] = incoming;
    } else {
      serialLength = 0;
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(TFT_BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  playBootAnimation();
  drawStaticScreen();
  drawMetadata();
  drawLyrics();
  drawProgress();
  Serial.println("READY");
}

void loop() {
  readSerialMessages();

  if (spotifyConnected && millis() - lastMessageMs > SERIAL_TIMEOUT_MS) {
    spotifyConnected = false;
  }

  const bool metadataChanged =
      spotifyConnected != renderedConnected || spotifyPlaying != renderedPlaying ||
      strcmp(trackId, renderedTrackId) != 0;
  if (metadataChanged) {
    drawMetadata();
    drawLyrics();
    copyField(renderedTrackId, sizeof(renderedTrackId), trackId);
    renderedConnected = spotifyConnected;
    renderedPlaying = spotifyPlaying;
  }

  const bool lyricsChanged =
      strcmp(lyricStatus, renderedLyricStatus) != 0 ||
      strcmp(currentLyric, renderedCurrentLyric) != 0;
  if (lyricsChanged) {
    drawLyrics();
    copyField(renderedLyricStatus, sizeof(renderedLyricStatus), lyricStatus);
    copyField(renderedCurrentLyric, sizeof(renderedCurrentLyric), currentLyric);
  }

  if (millis() - lastProgressDrawMs >= 250) {
    drawProgress();
    lastProgressDrawMs = millis();
  }

  delay(5);
}
