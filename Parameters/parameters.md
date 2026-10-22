<div dir="rtl">

# פרמטרים והגדרות

יש שלושה סוגים של פרמטרים: הגדרות שמשנים ממרכז הבקרה תוך כדי עבודה (בלי לקמפל), פרמטרים קבועים בקוד (`ESP32/parameters.h`), וסודות (`ESP32/SECRETS.h`).

## 1. הגדרות שמשנים ממרכז הבקרה, בלי קימפול

כל אלה נשמרות ב-flash של הלוח (NVS) ונשארות אחרי Reset או כיבוי. אפשר לשנות אותן גם בפקודה ב-Serial.

| הגדרה | איפה במרכז הבקרה | פקודה | טווח | ברירת מחדל |
|---|---|---|---|---|
| רשת Wi-Fi (שם וסיסמה) | Device controls ← Wi-Fi network | `WIFI_SSID <name>`, ואז `WIFI_PASSWORD <password>` | 2.4GHz בלבד, שם עד 32 בתים, סיסמה 8-64 בתים (אות עברית היא 2) או בלי. רווחים בקצוות הסיסמה נשמרים; סיסמה של רווחים בלבד = רשת פתוחה | הרשת מ-`SECRETS.h` |
| חזרה לרשת ברירת המחדל | Wi-Fi network ← Back to default | `WIFI_FORGET` | | |
| עוצמת הרמקול | Speaker volume | `VOLUME <0-100>` | 0-100% (בקול: 10-100%) | 100% |
| השתקה | Mute | `MUTE on\|off` | | כבוי |
| בהירות ה-LED | Brightness | `LED_BRIGHTNESS <0-100>` | 0-100% | 100% |
| ספי חיישן האור (ערב, יום) | Evening from / Day from ← Apply thresholds | `LIGHT_BANDS <evening> <day>` | 0-6142 | 1000 / 3000 |
| Hands-free | המתג ליד Talk | `VAD on\|off` | | כבוי |
| רגישות ה-Hands-free | Sensitivity | `VAD_SENSITIVITY <1-5>` | 1 (רק קול חזק) עד 5 | 3 |
| שקט שמסיים משפט | Stop after silence | `VAD_HANGOVER <600-1500>` | 0.6-1.5 שניות | 1 שנייה |
| כפתור BOOT ככפתור Talk | On the board itself | `BUTTON on\|off` | | **כבוי** |
| צלילי מצב (offline / online / שאלה שנכשלה) | On the board itself | `SOUNDS on\|off` | | **כבוי** |

הגדרות זמניות, שלא נשמרות: מצב ה-LED (`LED on|off`, הלוח תמיד עולה עם LED כבוי), וערך חיישן האור המדומה (`LIGHT <0-6142>`, רק כשאין LDR מחובר).

## 2. פרמטרים קבועים בקוד: `ESP32/parameters.h`

שינוי שלהם דורש קימפול וצריבה. ב-`parameters.h` מרוכזים כל הפרמטרים שמשתמש עשוי לרצות לכוון, וליד כל אחד הערה שמסבירה מה הוא עושה. קבועים פנימיים של האלגוריתמים (ספי גלאי הדיבור, גדלי מאגרים, צלילים) נשארו ליד הקוד שלהם, והם מפורטים בטבלה "קבועים פנימיים בקוד" למטה.

| פרמטר | ערך | מה הוא קובע |
|---|---|---|
| `I2S_BCLK`, `I2S_LRC`, `I2S_DOUT`, `I2S_MIC_SERIAL_DATA` | 26, 25, 22, 21 | פיני ה-I2S (משותפים למיקרופון ולמגבר) |
| `SD_CS`, `SD_SCLK`, `SD_MISO`, `SD_MOSI` | 5, 18, 19, 23 | פיני כרטיס ה-SD |
| `LED_PIN`, `LED_ACTIVE_HIGH` | 2, 1 | ה-LED שהעוזר שולט בו |
| `TALK_BUTTON_PIN` | 0 | כפתור BOOT |
| `LIGHT_SENSOR_PIN` | (בהערה) | מוגדר: הלוח קורא LDR אמיתי. לא מוגדר: חיישן מדומה. רק פיני ADC1 (‏32-39) |
| `OPENAI_MODEL` | `gpt-realtime-2.1-mini` | המודל שעונה |
| `OPENAI_VOICE` | `alloy` | הקול של העוזר |
| `OPENAI_TRANSCRIPTION_MODEL` | `gpt-4o-mini-transcribe` | המודל שכותב מה המשתמש אמר ("You said" ובועת הצ'אט). לא משנה את התשובה |
| `TIMEZONE_POSIX` | `IST-2IDT,M3.5.5/2,M10.5.0` | אזור הזמן: ישראל, עם מעבר אוטומטי לשעון קיץ (UTC+3) ובחזרה. כשהוא מוגדר הוא קובע |
| `TIMEZONE_UTC_OFFSET_SEC` | UTC+2 | רק אם מוחקים את `TIMEZONE_POSIX`: הפרש קבוע מ-UTC, בלי שעון קיץ |
| `kSerialBaudRate` | 115200 | חייב להתאים למרכז הבקרה |
| `kAliveIntervalMs` | 5000 | כל כמה זמן הלוח שולח `@@ALIVE <שניות מההדלקה>`, כדי שמרכז הבקרה יבחין בין לוח שעובד ללוח תקוע (הוא מחכה 15 שניות, אז צריך להישאר הרבה מתחת) |
| `kManualRecordDurationSec` | 3 | כמה זמן מקליט Talk / LISTEN / BOOT |
| `kMicSoftwareGain` | 2 | הגברה של המיקרופון לפני השליחה (x4 גרם לקיטום) |
| `kHandsFreeCooldownMs` | 600 | אחרי שהלוח מדבר, המיקרופון מחכה (כדי לא לשמוע את עצמו) |
| `kHandsFreeMaxDiscardsInARow`, `kHandsFreeDiscardPauseMs` | 3, 5000 | אחרי 3 רעשים ברצף שלא היו דיבור, ה-Hands-free מפסיק להקשיב ל-5 שניות |
| `kDefaultVadSensitivity` | 3 | רגישות ה-Hands-free עד שמשנים ושומרים (1-5) |
| `kMinVadHangoverMs`, `kMaxVadHangoverMs`, `kDefaultVadHangoverMs` | 600, 1500, 1000 | השקט שמסיים משפט ב-Hands-free: הטווח שמותר לבחור, וברירת המחדל |
| `kMaxTypedQuestionBytes` | 200 | אורך מקסימלי לשאלה מוקלדת (ASK), **בבתים** של UTF-8: אות עברית היא 2 בתים, כלומר כ-100 אותיות. שאלה ארוכה יותר נחתכת (בלי לשבור תו) |
| `kReplyTimeoutMs` | 15000 | שקט מהשרת שאחריו שאלה נכשלת, והחיבור ל-OpenAI נסגר ונפתח מחדש |
| `kStalledReplyRecordingTimeoutMs` | 20000 | הקלטת תשובה ב-SD שהאודיו שלה נעצר ולא הסתיימה נסגרת אחרי הזמן הזה |
| `kWifiBootWaitMs` / `kTimeSyncBootWaitMs` | 20000 / 15000 | כמה זמן ההדלקה מחכה ל-Wi-Fi ולשעון לפני שממשיכה בלעדיהם |
| `kWifiRestartConnectAfterMs` | 60000 | אם ה-Wi-Fi לא מתחבר, מתחילים את החיבור מחדש כל דקה |
| `kNtpServer1`, `kNtpServer2` | `pool.ntp.org`, `time.nist.gov` | שרתי השעה (SNTP) |
| `kWifiAttemptsBeforeFallback` | 2 | כמה ניסיונות חיבור כושלים ברצף לרשת ששמרו ממרכז הבקרה (ההמתנה בהדלקה, ואז ניסיון בדקה) עד שהלוח עובר לרשת מ-`SECRETS.h`, ובחזרה באותו אופן. רק בזיכרון: הרשת השמורה נשארת ב-flash להדלקה הבאה |
| `kOpenAiAttemptsBeforeOffline` | 5 | ניסיונות חיבור ברצף עד מצב offline |
| `kInitialReconnectBackoffMs` | 1000 | ההמתנה לפני הניסיון הראשון להתחבר מחדש ל-OpenAI (מוכפלת בכל ניסיון: 1, 2, 4, 8 שניות) |
| `kOfflineRetryIntervalMs` | 30000 | כל כמה זמן מנסים שוב במצב offline (כשה-Wi-Fi חוזר, מנסים מיד) |
| `kHeartbeatIntervalMs`, `kHeartbeatPongTimeoutMs`, `kHeartbeatMissedPongs` | 15000, 10000, 2 | ping ל-OpenAI כל 15 שניות; אם 2 תשובות (pong) ברצף לא חוזרות תוך 10 שניות, החיבור נסגר ומתחבר מחדש (מזהה חיבור "תקוע" כשיש Wi-Fi בלי אינטרנט). 0 מכבה |
| `kDefaultVolumePercent`, `kDefaultLedBrightnessPercent` | 100, 100 | ברירות מחדל עד שמשנים ושומרים |
| `kDefaultEveningLightMin`, `kDefaultDayLightMin` | 1000, 3000 | ספי האור ההתחלתיים |
| `kMinVoiceVolumePercent`, `kVoiceVolumeStepPercent` | 10, 20 | העוצמה הנמוכה ביותר בקול, וכמה "louder" מגביר |
| `kMaxTimerSeconds` | 3600 | הטיימר הארוך ביותר |
| `kTimerAlarmBeeps`, `kTimerAlarmGapMs` | 3, 150 | כשהטיימר מצלצל: 3 צפצופים עם הבהוב LED, 150ms ביניהם |
| `kMaxKeptRecordings` | 5 | כמה פקודות וכמה תשובות נשמרות ב-SD |
| `kSdSpiFrequencyHz` | 4MHz | מהירות ה-SPI של כרטיס ה-SD |
| `kButtonSampleMs`, `kButtonDebounceSamples` | 5, 6 | ה-debounce של כפתור BOOT: נדגם כל 5ms בטיימר, ולחיצה נספרת אחרי 6 דגימות (‏30ms) |

### קבועים פנימיים בקוד

אלה נשארו בתוך המודולים, כי הם חלק מהאלגוריתם (או מגבולות הזיכרון) ושינוי שלהם דורש הבנה של הקוד סביבם:

| קבוע | קובץ | ערך | מה הוא קובע |
|---|---|---|---|
| `kSampleRateHz` | `ESP32.ino`, `i2s_audio_utils.cpp`, `vad_utils.h` | 24000 | קצב הדגימה של המיקרופון (המינימום של OpenAI ל-PCM) |
| `kChunkSamples` | `ESP32.ino` | 2400 | גודל חתיכת אודיו שנשלחת ל-OpenAI: 100ms |
| `kRingBufferBytes` | `i2s_audio_utils.cpp` | 32KB | מאגר הניגון (כ-4 שניות mu-law), משותף עם המיקרופון |
| `kI2sDmaBufCount`, `kI2sDmaBufLen` | `i2s_audio_utils.cpp` | 12, 300 | מאגר ה-DMA של ה-I2S: 150ms |
| `kBeepFrequencyHz`, `kBeepDurationMs` | `i2s_audio_utils.cpp` | 880, 300 | צפצוף הבדיקה (BEEP) וצפצופי הטיימר |
| `kLowToneHz`, `kHighToneHz`, `kStatusToneMs`, `kStatusToneGapMs` | `device_controls_utils.cpp` | 440, 660, 120, 60 | צלילי המצב offline / online |
| `kFailedToneHz`, `kFailedToneMs` | `device_controls_utils.cpp` | 330, 250 | צליל "שאלה נכשלה" |
| `kLedPwmFrequencyHz`, `kLedPwmResolutionBits` | `board_state_utils.cpp` | 5000, 8 | ה-PWM שמעמעם את ה-LED |
| `kSensorReadIntervalMs`, `kSamplesPerReading`, `kAnnounceChange` | `light_sensor_utils.cpp` | 1000, 8, 150 | קריאת LDR אמיתי (רק עם `LIGHT_SENSOR_PIN`): פעם בשנייה, ממוצע של 8 קריאות, ומדווחים רק שינוי של 150 ומעלה |
| `kMaxLightSensorValue` | `light_sensor_utils.h` | 6142 | הערך הגבוה ביותר של חיישן האור (4095 כפול 1.5) |
| `kMaxLabelLength` | `clock_timer_utils.cpp` | 48 | אורך מקסימלי לתיאור של תזכורת ("check the oven") |
| `kTimePrefixChancePercent` | `greeting_utils.cpp` | 20 | הסיכוי שהברכה בהדלקה תתחיל ב-"Good morning/afternoon/evening" |
| `kSignalReportIntervalMs`, `kStillWaitingReportMs` | `wifi_utils.cpp` | 10000, 15000 | כל כמה זמן מדווחים את עוצמת ה-Wi-Fi, ו"עדיין מחכה" בזמן התחברות |
| `kWifiReturnFastRetryMs` | `ESP32.ino` | 10000 | כשה-Wi-Fi חוזר, מנסים להתחבר ל-OpenAI כל שנייה במשך 10 שניות, ואז חוזרים לקצב ה-offline |
| `kHandsFreeTurnsToPass`, `kMinLowestHeapBytes` | `ESP32.ino` | 5, 20KB | תנאי ה-PASS של בדיקת שלב 30 ("HANDS-FREE PASSED"): 5 שאלות Hands-free ברצף, והזיכרון הפנוי לא ירד מתחת ל-20KB |
| `kPreRollMs` | `vad_utils.h` | 500 | כמה אודיו מלפני תחילת הדיבור נשלח (ה-pre-roll) |
| `kMaxUtteranceMs` | `vad_utils.h` | 10000 | משפט ארוך ביותר ב-Hands-free |
| `kMinSensitivity`, `kMaxSensitivity` | `vad_utils.h` | 1, 5 | טווח הרגישות (קשור לטבלאות הספים בשורה הבאה) |
| `kActivationMarginDb[]`, `kMinActiveLevelDbfs[]` | `vad_utils.cpp` | 16 עד 6 dB, ‎-55 עד ‎-75 dBFS | לכל רגישות: כמה מעל רעש הרקע, ומה העוצמה המינימלית, כדי שמסגרת של 20ms תיחשב פעילה |
| `kMinSpeechBandShare`, `kMaxSpeechZeroCrossingRate`, `kMinVoicing` | `vad_utils.cpp` | 0.35, 0.30, 0.60 | מה נחשב קול אנושי: חלק האנרגיה בתחום הדיבור, רחש, וקוליות (pitch) |
| `kOnsetMinActiveFrames`, `kOnsetMinVoicedFrames` | `vad_utils.cpp` | 8, 4 | תחילת דיבור: 8 מסגרות פעילות (160ms) ו-4 קוליות (80ms) מתוך 240ms |
| `kMinVoicedFramesToSend`, `kMinVoicedSharePercent`, `kMinDipSharePercent` | `vad_utils.cpp` | 6, 25%, 12% | משפט שלא עומד בהם נזרק כרעש ולא נשלח |
| `kWarmupFrames` | `vad_utils.cpp` | 15 | 300ms בהתחלה רק ללמידת רעש הרקע |
| שאר ספי הגלאי | `vad_utils.cpp` (שורות 30-78) | | ה-hysteresis וזיהוי ההברות; מוסברים בהערות שם |
| `kTaskStackBytes`, `kTaskPriority`, `kTaskCore` | `mic_capture_utils.cpp` | 4096, 3, 1 | המשימה שקוראת את המיקרופון |
| `kSessionUpdateMessage` | `ESP32.ino` | | ההוראות למודל ורשימת הכלים |

## 3. סודות: `ESP32/SECRETS.h`

| סוד | מה |
|---|---|
| `WIFI_SSID`, `WIFI_PASSWORD` | רשת ברירת המחדל (אפשר להחליף ממרכז הבקרה) |
| `OPENAI_API_KEY` | מפתח ל-OpenAI Realtime API |

הקובץ ב-`.gitignore`. בגיט נמצאת רק התבנית `ESP32/SECRETS.example.h`: מעתיקים אותה ל-`SECRETS.h` וממלאים. בלי `SECRETS.h` הקומפילציה נעצרת עם הודעה שמסבירה מה לעשות.

</div>
