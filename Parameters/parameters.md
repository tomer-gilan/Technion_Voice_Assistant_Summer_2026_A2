<div dir="rtl">

# פרמטרים והגדרות

יש שלושה סוגים של פרמטרים: הגדרות שמשנים ממרכז הבקרה תוך כדי עבודה (בלי לקמפל), פרמטרים קבועים בקוד (`ESP32/parameters.h`), וסודות (`ESP32/SECRETS.h`).

## 1. הגדרות שמשנים ממרכז הבקרה, בלי קימפול

כל אלה נשמרות ב-flash של הלוח (NVS) ונשארות אחרי Reset או כיבוי. אפשר לשנות אותן גם בפקודה ב-Serial.

<table dir="rtl">
<thead>
<tr><th>הגדרה</th><th>איפה במרכז הבקרה</th><th>פקודה</th><th>טווח</th><th>ברירת מחדל</th></tr>
</thead>
<tbody>
<tr><td>רשת Wi-Fi (שם וסיסמה)</td><td>Device controls ← Wi-Fi network</td><td><code>WIFI_SSID &lt;name&gt;</code>, ואז <code>WIFI_PASSWORD &lt;password&gt;</code></td><td>2.4GHz בלבד, שם עד 32 בתים, סיסמה 8-64 בתים (אות עברית היא 2) או בלי. רווחים בקצוות הסיסמה נשמרים; סיסמה של רווחים בלבד = רשת פתוחה</td><td>הרשת מ-<code>SECRETS.h</code></td></tr>
<tr><td>חזרה לרשת ברירת המחדל</td><td>Wi-Fi network ← Back to default</td><td><code>WIFI_FORGET</code></td><td></td><td></td></tr>
<tr><td>מפתח OpenAI API</td><td>Device controls ← OpenAI API key ← Save &amp; connect</td><td><code>OPENAI_KEY &lt;key&gt;</code></td><td><code>sk-</code> ואחריו אותיות, ספרות, <code>-</code> ו-<code>_</code>, באורך 20-200. הלוח מתחבר מחדש מיד, ומציג רק את 3 התווים הראשונים ו-4 האחרונים</td><td>המפתח מ-<code>SECRETS.h</code></td></tr>
<tr><td>חזרה למפתח ברירת המחדל</td><td>OpenAI API key ← Back to default</td><td><code>OPENAI_KEY_FORGET</code></td><td></td><td></td></tr>
<tr><td>עוצמת הרמקול</td><td>Speaker volume</td><td><code>VOLUME &lt;0-100&gt;</code></td><td>0-100% (בקול: 10-100%)</td><td>100%</td></tr>
<tr><td>השתקה</td><td>Mute</td><td><code>MUTE on|off</code></td><td></td><td>כבוי</td></tr>
<tr><td>בהירות ה-LED</td><td>Brightness</td><td><code>LED_BRIGHTNESS &lt;0-100&gt;</code></td><td>0-100%</td><td>100%</td></tr>
<tr><td>ספי חיישן האור (ערב, יום)</td><td>Evening from / Day from ← Apply thresholds</td><td><code>LIGHT_BANDS &lt;evening&gt; &lt;day&gt;</code></td><td>0-6142</td><td>1000 / 3000</td></tr>
<tr><td>Hands-free</td><td>המתג ליד Talk, או בקול ("Switch to hands-free mode")</td><td><code>VAD on|off</code></td><td></td><td>כבוי</td></tr>
<tr><td>רגישות ה-Hands-free</td><td>Sensitivity</td><td><code>VAD_SENSITIVITY &lt;1-5&gt;</code></td><td>1 (רק קול חזק) עד 5</td><td>3</td></tr>
<tr><td>שקט שמסיים משפט</td><td>Stop after silence</td><td><code>VAD_HANGOVER &lt;600-1500&gt;</code></td><td>0.6-1.5 שניות</td><td>1 שנייה</td></tr>
<tr><td>כפתור BOOT ככפתור Talk</td><td>On the board itself</td><td><code>BUTTON on|off</code></td><td></td><td><strong>כבוי</strong></td></tr>
<tr><td>צלילי מצב (offline / online / שאלה שנכשלה)</td><td>On the board itself</td><td><code>SOUNDS on|off</code></td><td></td><td><strong>כבוי</strong></td></tr>
</tbody>
</table>

הגדרות זמניות, שלא נשמרות: מצב ה-LED (`LED on|off`, הלוח תמיד עולה עם LED כבוי), וערך חיישן האור המדומה (`LIGHT <0-6142>`, רק כשאין LDR מחובר). מרכז הבקרה שולח לבד גם `LAPTOP_PLAYBACK on <ms>|off` בזמן שהנגן שלו מנגן הקלטה, כדי שה-Hands-free לא יקשיב לה.

## 2. פרמטרים קבועים בקוד: `ESP32/parameters.h`

שינוי שלהם דורש קימפול וצריבה. `parameters.h` הוא ה-`config_parameters.h` של הפרויקט: בו מרוכזים כל הפרמטרים שמשתמש עשוי לרצות לכוון, וליד כל אחד הערה שמסבירה מה הוא עושה. קבועים פנימיים של האלגוריתמים (ספי גלאי הדיבור, גדלי מאגרים, צלילים) נשארו ליד הקוד שלהם, והם מפורטים בטבלה "קבועים פנימיים בקוד" למטה.

<table dir="rtl">
<thead>
<tr><th>פרמטר</th><th>ערך</th><th>מה הוא קובע</th></tr>
</thead>
<tbody>
<tr><td><code>I2S_BCLK</code>, <code>I2S_LRC</code>, <code>I2S_DOUT</code>, <code>I2S_MIC_SERIAL_DATA</code></td><td>26, 25, 22, 21</td><td>פיני ה-I2S (משותפים למיקרופון ולמגבר)</td></tr>
<tr><td><code>SD_CS</code>, <code>SD_SCLK</code>, <code>SD_MISO</code>, <code>SD_MOSI</code></td><td>5, 18, 19, 23</td><td>פיני כרטיס ה-SD</td></tr>
<tr><td><code>LED_PIN</code>, <code>LED_ACTIVE_HIGH</code></td><td>2, 1</td><td>ה-LED שהעוזר שולט בו</td></tr>
<tr><td><code>TALK_BUTTON_PIN</code></td><td>0</td><td>כפתור BOOT</td></tr>
<tr><td><code>LIGHT_SENSOR_PIN</code></td><td>(בהערה)</td><td>מוגדר: הלוח קורא LDR אמיתי. לא מוגדר: חיישן מדומה. רק פיני ADC1 (‏32-39)</td></tr>
<tr><td><code>OPENAI_MODEL</code></td><td><code>gpt-realtime-2.1-mini</code></td><td>המודל שעונה</td></tr>
<tr><td><code>OPENAI_VOICE</code></td><td><code>alloy</code></td><td>הקול של העוזר</td></tr>
<tr><td><code>OPENAI_TRANSCRIPTION_MODEL</code></td><td><code>gpt-4o-mini-transcribe</code></td><td>המודל שכותב מה המשתמש אמר ("You said" ובועת הצ'אט). לא משנה את התשובה</td></tr>
<tr><td><code>TIMEZONE_POSIX</code></td><td><code>IST-2IDT,M3.5.5/2,M10.5.0</code></td><td>אזור הזמן: ישראל, עם מעבר אוטומטי לשעון קיץ (UTC+3) ובחזרה. כשהוא מוגדר הוא קובע</td></tr>
<tr><td><code>TIMEZONE_UTC_OFFSET_SEC</code></td><td>UTC+2</td><td>רק אם מוחקים את <code>TIMEZONE_POSIX</code>: הפרש קבוע מ-UTC, בלי שעון קיץ</td></tr>
<tr><td><code>kSerialBaudRate</code></td><td>115200</td><td>חייב להתאים למרכז הבקרה</td></tr>
<tr><td><code>kAliveIntervalMs</code></td><td>5000</td><td>כל כמה זמן הלוח שולח <code>@@ALIVE &lt;שניות מההדלקה&gt;</code>, כדי שמרכז הבקרה יבחין בין לוח שעובד ללוח תקוע (הוא מחכה 15 שניות, אז צריך להישאר הרבה מתחת)</td></tr>
<tr><td><code>kManualRecordDurationSec</code></td><td>3</td><td>כמה זמן מקליט Talk / LISTEN / BOOT</td></tr>
<tr><td><code>kMicSoftwareGain</code></td><td>2</td><td>הגברה של המיקרופון לפני השליחה (x4 גרם לקיטום)</td></tr>
<tr><td><code>kHandsFreeCooldownMs</code></td><td>600</td><td>אחרי שהלוח מדבר, המיקרופון מחכה (כדי לא לשמוע את עצמו)</td></tr>
<tr><td><code>kHandsFreeMaxDiscardsInARow</code>, <code>kHandsFreeDiscardPauseMs</code></td><td>3, 5000</td><td>אחרי 3 רעשים ברצף שלא היו דיבור, ה-Hands-free מפסיק להקשיב ל-5 שניות</td></tr>
<tr><td><code>kDefaultVadSensitivity</code></td><td>3</td><td>רגישות ה-Hands-free עד שמשנים ושומרים (1-5)</td></tr>
<tr><td><code>kMinVadHangoverMs</code>, <code>kMaxVadHangoverMs</code>, <code>kDefaultVadHangoverMs</code></td><td>600, 1500, 1000</td><td>השקט שמסיים משפט ב-Hands-free: הטווח שמותר לבחור, וברירת המחדל</td></tr>
<tr><td><code>kMaxTypedQuestionBytes</code></td><td>200</td><td>אורך מקסימלי לשאלה מוקלדת (ASK), <strong>בבתים</strong> של UTF-8: אות עברית היא 2 בתים, כלומר כ-100 אותיות. שאלה ארוכה יותר נחתכת (בלי לשבור תו)</td></tr>
<tr><td><code>kReplyTimeoutMs</code></td><td>15000</td><td>שקט מהשרת שאחריו שאלה נכשלת, והחיבור ל-OpenAI נסגר ונפתח מחדש</td></tr>
<tr><td><code>kStalledReplyRecordingTimeoutMs</code></td><td>20000</td><td>הקלטת תשובה ב-SD שהאודיו שלה נעצר ולא הסתיימה נסגרת אחרי הזמן הזה</td></tr>
<tr><td><code>kWifiBootWaitMs</code> / <code>kTimeSyncBootWaitMs</code></td><td>20000 / 15000</td><td>כמה זמן ההדלקה מחכה ל-Wi-Fi ולשעון לפני שממשיכה בלעדיהם</td></tr>
<tr><td><code>kWifiFailedAttemptAfterMs</code></td><td>60000</td><td>דקה בלי Wi-Fi באותה רשת נספרת כניסיון חיבור כושל אחד (בשביל <code>kWifiAttemptsBeforeFallback</code>). בינתיים הלוח מנסה להתחבר שוב כל כמה שניות</td></tr>
<tr><td><code>kNtpServer1</code>, <code>kNtpServer2</code></td><td><code>pool.ntp.org</code>, <code>time.nist.gov</code></td><td>שרתי השעה (SNTP)</td></tr>
<tr><td><code>kWifiAttemptsBeforeFallback</code></td><td>2</td><td>כמה ניסיונות חיבור כושלים ברצף לרשת ששמרו ממרכז הבקרה (ההמתנה בהדלקה, ואז כל דקה בלי חיבור) עד שהלוח עובר לרשת מ-<code>SECRETS.h</code>, ובחזרה באותו אופן. רק בזיכרון: הרשת השמורה נשארת ב-flash להדלקה הבאה</td></tr>
<tr><td><code>kOpenAiKeyRefusalsBeforeFallback</code></td><td>2</td><td>אותו דבר למפתח ה-API ששמרו ממרכז הבקרה: כמה פעמים ברצף OpenAI דוחים אותו (HTTP 401 או 403 בפתיחת החיבור) עד שהלוח עובר למפתח מ-<code>SECRETS.h</code>, ובחזרה באותו אופן. רק בזיכרון: המפתח השמור נשאר ב-flash. הניסיונות ממשיכים בקצב הרגיל (1, 2, 4... שניות, ואז כל 30 שניות)</td></tr>
<tr><td><code>kOpenAiAttemptsBeforeOffline</code></td><td>5</td><td>ניסיונות חיבור ברצף עד מצב offline</td></tr>
<tr><td><code>kInitialReconnectBackoffMs</code></td><td>1000</td><td>ההמתנה לפני הניסיון הראשון להתחבר מחדש ל-OpenAI (מוכפלת בכל ניסיון: 1, 2, 4, 8 שניות)</td></tr>
<tr><td><code>kOfflineRetryIntervalMs</code></td><td>30000</td><td>כל כמה זמן מנסים שוב במצב offline (כשה-Wi-Fi חוזר, מנסים מיד)</td></tr>
<tr><td><code>kHeartbeatIntervalMs</code>, <code>kHeartbeatPongTimeoutMs</code>, <code>kHeartbeatMissedPongs</code></td><td>15000, 10000, 2</td><td>ping ל-OpenAI כל 15 שניות; אם 2 תשובות (pong) ברצף לא חוזרות תוך 10 שניות, החיבור נסגר ומתחבר מחדש (מזהה חיבור "תקוע" כשיש Wi-Fi בלי אינטרנט). 0 מכבה</td></tr>
<tr><td><code>kDefaultVolumePercent</code>, <code>kDefaultLedBrightnessPercent</code></td><td>100, 100</td><td>ברירות מחדל עד שמשנים ושומרים</td></tr>
<tr><td><code>kDefaultEveningLightMin</code>, <code>kDefaultDayLightMin</code></td><td>1000, 3000</td><td>ספי האור ההתחלתיים</td></tr>
<tr><td><code>kMinVoiceVolumePercent</code>, <code>kVoiceVolumeStepPercent</code></td><td>10, 20</td><td>העוצמה הנמוכה ביותר בקול, וכמה "louder" מגביר</td></tr>
<tr><td><code>kMaxTimerSeconds</code></td><td>3600</td><td>הטיימר הארוך ביותר</td></tr>
<tr><td><code>kTimerAlarmBeeps</code>, <code>kTimerAlarmGapMs</code></td><td>3, 150</td><td>כשהטיימר מצלצל: 3 צפצופים עם הבהוב LED, 150ms ביניהם</td></tr>
<tr><td><code>kMaxKeptRecordings</code></td><td>5</td><td>כמה פקודות וכמה תשובות נשמרות ב-SD</td></tr>
<tr><td><code>kSdSpiFrequencyHz</code></td><td>4MHz</td><td>מהירות ה-SPI של כרטיס ה-SD</td></tr>
<tr><td><code>kButtonSampleMs</code>, <code>kButtonDebounceSamples</code></td><td>5, 6</td><td>ה-debounce של כפתור BOOT: נדגם כל 5ms בטיימר, ולחיצה נספרת אחרי 6 דגימות (‏30ms)</td></tr>
<tr><td><code>kStartToneHz</code>, <code>kStartToneMs</code></td><td>1320, 60</td><td>"טיק" גבוה וקצר רגע לפני שהקלטה ידנית מתחילה (BOOT, Talk, LISTEN; לא ב-Hands-free). תמיד פועל, לא תלוי בצלילי המצב; שקט רק בהשתקה</td></tr>
<tr><td><code>kRefusedToneHz</code>, <code>kRefusedToneMs</code></td><td>220, 120</td><td>צליל נמוך וקצר כשלחיצה על BOOT לא יכולה להתחיל שאלה (offline, עוד לא מוכן, עסוק). תמיד פועל, כמו ה"טיק"</td></tr>
<tr><td><code>kTechnicianHoldMs</code></td><td>2000</td><td>בדיקת הטכנאי: כשכפתור BOOT מופעל, לחיצה ארוכה של 2 שניות משמיעה צפצוף אחד לכל בדיקת הדלקה, בסדר קבוע: Wi-Fi, OpenAI, כרטיס SD, מיקרופון</td></tr>
<tr><td><code>kTechnicianOkToneHz</code>, <code>kTechnicianFailToneHz</code>, <code>kTechnicianToneMs</code>, <code>kTechnicianGapMs</code></td><td>880, 220, 150, 150</td><td>הצפצופים של בדיקת הטכנאי: גבוה = תקין, נמוך = נכשל, 150ms כל אחד ולפחות 150ms שקט ביניהם (לפי המצב ברגע הלחיצה)</td></tr>
</tbody>
</table>

### קבועים פנימיים בקוד

אלה נשארו בתוך המודולים, כי הם חלק מהאלגוריתם (או מגבולות הזיכרון) ושינוי שלהם דורש הבנה של הקוד סביבם:

<table dir="rtl">
<thead>
<tr><th>קבוע</th><th>קובץ</th><th>ערך</th><th>מה הוא קובע</th></tr>
</thead>
<tbody>
<tr><td><code>kSampleRateHz</code></td><td><code>vad_utils.h</code> (ו-<code>ESP32.ino</code>, <code>i2s_audio_utils.cpp</code> משתמשים בו)</td><td>24000</td><td>קצב הדגימה של המיקרופון (המינימום של OpenAI ל-PCM)</td></tr>
<tr><td><code>kChunkSamples</code></td><td><code>ESP32.ino</code></td><td>2400</td><td>גודל חתיכת אודיו שנשלחת ל-OpenAI: 100ms</td></tr>
<tr><td><code>kRingBufferBytes</code></td><td><code>i2s_audio_utils.cpp</code></td><td>32KB</td><td>מאגר הניגון (כ-4 שניות mu-law), משותף עם המיקרופון</td></tr>
<tr><td><code>kI2sDmaBufCount</code>, <code>kI2sDmaBufLen</code></td><td><code>i2s_audio_utils.cpp</code></td><td>12, 300</td><td>מאגר ה-DMA של ה-I2S: 150ms</td></tr>
<tr><td><code>kBeepFrequencyHz</code>, <code>kBeepDurationMs</code></td><td><code>i2s_audio_utils.cpp</code></td><td>880, 300</td><td>צפצוף הבדיקה (BEEP) וצפצופי הטיימר</td></tr>
<tr><td><code>kPlayedOutMarginMs</code></td><td><code>i2s_audio_utils.cpp</code></td><td>40</td><td>אחרי "טיק" ההתחלה ההקלטה מחכה שהצליל ייצא מהרמקול: אורך מאגר ה-DMA (‏150ms) ועוד המרווח הזה, כדי שה"טיק" לא ייכנס להקלטה</td></tr>
<tr><td><code>kLowToneHz</code>, <code>kHighToneHz</code>, <code>kStatusToneMs</code>, <code>kStatusToneGapMs</code></td><td><code>device_controls_utils.cpp</code></td><td>440, 660, 120, 60</td><td>צלילי המצב offline / online</td></tr>
<tr><td><code>kFailedToneHz</code>, <code>kFailedToneMs</code></td><td><code>device_controls_utils.cpp</code></td><td>330, 250</td><td>צליל "שאלה נכשלה"</td></tr>
<tr><td><code>kLedPwmFrequencyHz</code>, <code>kLedPwmResolutionBits</code></td><td><code>board_state_utils.cpp</code></td><td>5000, 8</td><td>ה-PWM שמעמעם את ה-LED</td></tr>
<tr><td><code>kSensorReadIntervalMs</code>, <code>kSamplesPerReading</code>, <code>kAnnounceChange</code></td><td><code>light_sensor_utils.cpp</code></td><td>1000, 8, 150</td><td>קריאת LDR אמיתי (רק עם <code>LIGHT_SENSOR_PIN</code>): פעם בשנייה, ממוצע של 8 קריאות, ומדווחים רק שינוי של 150 ומעלה</td></tr>
<tr><td><code>kMaxLightSensorValue</code></td><td><code>light_sensor_utils.h</code></td><td>6142</td><td>הערך הגבוה ביותר של חיישן האור (4095 כפול 1.5)</td></tr>
<tr><td><code>kMaxLabelLength</code></td><td><code>clock_timer_utils.cpp</code></td><td>48</td><td>אורך מקסימלי לתיאור של תזכורת ("check the oven")</td></tr>
<tr><td><code>kTimePrefixChancePercent</code></td><td><code>greeting_utils.cpp</code></td><td>20</td><td>הסיכוי שהברכה בהדלקה תתחיל ב-"Good morning/afternoon/evening"</td></tr>
<tr><td><code>kSignalReportIntervalMs</code>, <code>kStillWaitingReportMs</code></td><td><code>wifi_utils.cpp</code></td><td>10000, 15000</td><td>כל כמה זמן מדווחים את עוצמת ה-Wi-Fi, ו"עדיין מחכה" בזמן התחברות</td></tr>
<tr><td><code>kRetryIntervalMs</code>, <code>kAttemptTimeoutMs</code></td><td><code>wifi_utils.cpp</code></td><td>5000, 15000</td><td>כשה-Wi-Fi למטה הלוח מתחיל ניסיון חיבור חדש בעצמו (החיבור מחדש האוטומטי של ליבת ה-ESP32 כבוי): 5 שניות אחרי שהניסיון הקודם התחיל, אם הוא נכשל, או אחרי 15 שניות בלי תוצאה</td></tr>
<tr><td><code>kMaxSsidLength</code>, <code>kMinPasswordLength</code>, <code>kMaxPasswordLength</code></td><td><code>wifi_utils.h</code></td><td>32, 8, 64</td><td>גבולות שם רשת ה-Wi-Fi והסיסמה, בבתים (מגבלות 802.11 ו-WPA2). הלוח שולח אותם למרכז הבקרה בשורת <code>@@LIMITS</code>, יחד עם שאר הגבולות שהדף בודק</td></tr>
<tr><td><code>kMinApiKeyLength</code>, <code>kMaxApiKeyLength</code></td><td><code>openai_realtime_utils.h</code></td><td>20, 200</td><td>גבולות האורך של מפתח API שאפשר לשמור ממרכז הבקרה (מפתח <code>sk-proj-</code> הוא 164 תווים). גם הם נשלחים ב-<code>@@LIMITS</code>. מפתח ארוך יותר ב-<code>SECRETS.h</code> עוצר את הקומפילציה עם הסבר</td></tr>
<tr><td><code>kWifiReturnFastRetryMs</code></td><td><code>ESP32.ino</code></td><td>10000</td><td>כשה-Wi-Fi חוזר, או כששומרים מפתח API חדש, מנסים להתחבר ל-OpenAI כל שנייה במשך 10 שניות, ואז חוזרים לקצב ה-offline</td></tr>
<tr><td><code>kMaxLaptopPlaybackHoldMs</code></td><td><code>hands_free_utils.h</code></td><td>5 דקות</td><td>הכי הרבה זמן ש-<code>LAPTOP_PLAYBACK on</code> עוצר את ה-Hands-free. מרכז הבקרה שולח כמה זמן נשאר להקלטה (ועוד 1.5 שניות), כך שזה רק גבול לדף שנסגר באמצע ניגון</td></tr>
<tr><td><code>kHandsFreeTurnsToPass</code>, <code>kMinLowestHeapBytes</code></td><td><code>ESP32.ino</code></td><td>5, 20KB</td><td>תנאי ה-PASS של בדיקת שלב 30 ("HANDS-FREE PASSED"): 5 שאלות Hands-free ברצף, והזיכרון הפנוי לא ירד מתחת ל-20KB</td></tr>
<tr><td><code>kPreRollMs</code></td><td><code>vad_utils.h</code></td><td>500</td><td>כמה אודיו מלפני תחילת הדיבור נשלח (ה-pre-roll)</td></tr>
<tr><td><code>kMaxUtteranceMs</code></td><td><code>vad_utils.h</code></td><td>10000</td><td>משפט ארוך ביותר ב-Hands-free</td></tr>
<tr><td><code>kMinSensitivity</code>, <code>kMaxSensitivity</code></td><td><code>vad_utils.h</code></td><td>1, 5</td><td>טווח הרגישות (קשור לטבלאות הספים בשורה הבאה)</td></tr>
<tr><td><code>kActivationMarginDb[]</code>, <code>kMinActiveLevelDbfs[]</code></td><td><code>vad_utils.cpp</code></td><td>16 עד 6 dB, ‎-55 עד ‎-75 dBFS</td><td>לכל רגישות: כמה מעל רעש הרקע, ומה העוצמה המינימלית, כדי שמסגרת של 20ms תיחשב פעילה</td></tr>
<tr><td><code>kMinSpeechBandShare</code>, <code>kMaxSpeechZeroCrossingRate</code>, <code>kMinVoicing</code></td><td><code>vad_utils.cpp</code></td><td>0.35, 0.30, 0.60</td><td>מה נחשב קול אנושי: חלק האנרגיה בתחום הדיבור, רחש, וקוליות (pitch)</td></tr>
<tr><td><code>kOnsetMinActiveFrames</code>, <code>kOnsetMinVoicedFrames</code></td><td><code>vad_utils.cpp</code></td><td>8, 4</td><td>תחילת דיבור: 8 מסגרות פעילות (160ms) ו-4 קוליות (80ms) מתוך 240ms</td></tr>
<tr><td><code>kMinVoicedFramesToSend</code>, <code>kMinVoicedSharePercent</code>, <code>kMinDipSharePercent</code></td><td><code>vad_utils.cpp</code></td><td>6, 25%, 12%</td><td>משפט שלא עומד בהם נזרק כרעש ולא נשלח</td></tr>
<tr><td><code>kWarmupFrames</code></td><td><code>vad_utils.cpp</code></td><td>15</td><td>300ms בהתחלה רק ללמידת רעש הרקע</td></tr>
<tr><td>שאר ספי הגלאי</td><td><code>vad_utils.cpp</code> (שורות 30-78)</td><td></td><td>ה-hysteresis וזיהוי ההברות; מוסברים בהערות שם</td></tr>
<tr><td><code>kTaskStackBytes</code>, <code>kTaskPriority</code>, <code>kTaskCore</code></td><td><code>mic_capture_utils.cpp</code></td><td>4096, 3, 1</td><td>המשימה שקוראת את המיקרופון</td></tr>
<tr><td><code>kSessionUpdateMessage</code></td><td><code>realtime_session_config.h</code></td><td></td><td>ההוראות למודל, פורמט האודיו ורשימת הכלים (משפט אחד בכל שורה)</td></tr>
</tbody>
</table>

## 3. סודות: `ESP32/SECRETS.h`

<table dir="rtl">
<thead>
<tr><th>סוד</th><th>מה</th></tr>
</thead>
<tbody>
<tr><td><code>WIFI_SSID</code>, <code>WIFI_PASSWORD</code></td><td>רשת ברירת המחדל (אפשר להחליף ממרכז הבקרה)</td></tr>
<tr><td><code>OPENAI_API_KEY</code></td><td>מפתח ברירת המחדל ל-OpenAI Realtime API (אפשר להחליף ממרכז הבקרה)</td></tr>
</tbody>
</table>

הקובץ ב-`.gitignore`. בגיט נמצאת רק התבנית `ESP32/SECRETS.example.h`: מעתיקים אותה ל-`SECRETS.h` וממלאים. בלי `SECRETS.h` הקומפילציה נעצרת עם הודעה שמסבירה מה לעשות.

</div>
