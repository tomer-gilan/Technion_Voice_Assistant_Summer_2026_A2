<div dir="rtl">

# התקדמות - עבודת הלילה על הענף improvements

הענף `improvements` נפתח מ-`vad-auto-listen` ב-17.10.2026 בערב. כל שיפור הוא commit מקומי, בלי push. הקובץ הזה מתעדכן לאורך הלילה.

## מה בוצע

### מרכז הבקרה (`tools/reply_audio_app.html`)

| commit | מה |
|---|---|
| `001aa5f` | שיחה עשירה יותר: שעה לכל בועה, זמן תגובה, הערות על מה שהלוח עשה (LED, חיישן, רעש שנזרק, שאלה שנכשלה), כפתור Clear, עד 60 פריטים |
| `71fb5af` | עיגול מצב חי: טבעת שזזה לפי עוצמת הקול, פסי קול כשהעוזר מדבר. נורה שזוהרת לפי בהירות ה-LED, ואייקון יום / ערב / לילה |
| `8205d32` | גרף זמן תגובה לכל פקודה (עד OpenAI ועד הצליל הראשון), קו חציון, כרטיס מידע במעבר עכבר, ו-Export CSV |
| `768f593` | צ'ק-ליסט להדגמה בעברית (כפתור Demo checklist), נשמר בדפדפן |
| `9d83484` | תמיכה בכלי `set_volume` במסך: מצב, הערה בשיחה וסטטיסטיקה |
| `c448f14` | באנר אדום כשכבל ה-USB מתנתק או כשהלוח נעצר |
| `736724b` | כרטיס Try saying: דוגמאות לבקשות, לחיצה מתחילה הקלטה |
| `c3a7fbc` | הצ'ק-ליסט מעודכן לפי סיפורי המשתמש ומקרי הקצה החדשים |
| `ce4fc0c` | ספירה לאחור של הטיימר ליד המצב, והערות בשיחה על טיימר ושעה |
| `5dc56f7` | Stage view: מסך מלא גדול לקהל (כפתור או מקש F, יציאה ב-Esc). רווח עדיין מדבר |
| `2094aae` | Replay a log: מנגן לוג שמור עם התזמון שלו (1×, 4×, 20× או הכל בבת אחת). רק כשאין לוח מחובר |
| `528c58c` | Summary: מסך מלא לסוף ההדגמה עם הפקודות, אחוז ההצלחה, זמן התגובה, מה העוזר עשה וההגדרות של הלוח |
| `b2ebc3c` | Tour: סיור מודרך של 9 תחנות במסך, בשביל בוחנים שמפעילים לבד |
| `3db177b` | Save report: כפתור ב-Summary ששומר את הסשן כדף HTML אחד (סיכום, גרף, טבלת פקודות, שיחה) |
| `253fe9f` | Self-test: כפתור ב-Board health שבודק לפני הדגמה את הנורות, ה-Wi-Fi, ה-LED (מהבהב), הרמקול, חיישן האור, ה-SD ו-OpenAI. רק פקודות שכבר קיימות בקושחה |
| `d9de9bf` | הצ'ק-ליסט מסמן לבד מה שהלוח באמת עשה (הדלקה, שאלה, LED, עוצמה, טיימר וכו'), עם הודעה קטנה למטה. רק עם לוח מחובר, לא בניגון לוג |
| `47292dd` | שעון הדגמה: כפתור ב-Demo checklist מפעיל שעון בכותרת עם השלב לפי `docs/demo-plan.md`. אדום אחרי 25 דקות, לחיצה עוצרת |
| `4f070e9` | כפתור Save בשיחה: שומר את השיחה כקובץ טקסט. ופריט "טיימר בלי אינטרנט" ב-Demo checklist |

### קושחה (`test/`)

| commit | מה |
|---|---|
| `80b1506` | כלי קולי `set_volume`: עוצמה באחוזים (`Set the volume to 30 percent`) או מגביר / מנמיך ב-20% |
| `e0a8753` | מודול חדש `clock_timer_utils`: הכלים `get_current_time` (מה השעה) ו-`set_timer` (טיימר שמצפצף ומהבהב) |
| `9b6db09` | עמעום בקול: `Dim the light to 30 percent`. ל-`set_board_state` יש בהירות אופציונלית, שנשמרת כמו הסליידר |
| `22ca52c` | הטיימר מצלצל גם כשאין חיבור ל-OpenAI (קודם הוא חיכה לחיבור). ההודעה בקול רק כשיש חיבור |
| `d9af02e` | `How long is left on my timer?`: התשובה של `get_current_time` כוללת גם כמה זמן נשאר לטיימר |
| `3abf862` | `What can you do?` עונה במשפט אחד מה העוזר יודע לעשות. גם בכרטיס Try saying |
| `282dfa9` | תיקון קטן שהקומפיילר מצא: הבאפר של ההודעה בקול על הטיימר יכול היה להיות קצר מדי עם תיאור ארוך |
| `ff777f2` | LDR אמיתי: עם `#define LIGHT_SENSOR_PIN 34` ב-`config.h` הלוח קורא את החיישן כל שנייה. בלי השורה הזאת שום דבר לא משתנה (לא נגעתי ב-`config.h`) |
| `7bf4cea` | `How are you doing?`: כלי חדש `get_board_status` (מודול `board_info_utils`) שמחזיר עוצמת Wi-Fi, זמן מההדלקה וזיכרון פנוי. גם ב-Try saying וב-Summary |
| `20de4c7` | שאלה בכתב לחדר רועש: שדה מתחת ל-Talk ששולח `ASK <text>`. התשובה בקול כרגיל, וגם כלים עובדים. בבועה כתוב typed (גם ב-GUI) |
| `dd56064` | שעון קיץ: עם `#define TIMEZONE_POSIX "IST-2IDT,M3.5.5/2,M10.5.0"` ב-`config.h` השעה נכונה גם בקיץ. בלי השורה הכל כמו קודם (לא נגעתי ב-`config.h`) |
| `348a78d` | תזכורת בקול: ל-`set_timer` יש תיאור (`label`), ובסוף העוזר אומר `Time's up - check the oven`. גם מרכז הבקרה מציג את התיאור |

### תיעוד

| commit | מה |
|---|---|
| `08c9724` | README ויזואלי (צילום מסך, דיאגרמות מהמצגת, טבלת כלים, גרסאות הספריות), סיפור משתמש על עוצמה, 3 מקרי קצה חדשים, PDF-ים מחדש |
| `98a0876` | טיימר ושעה ב-README ובסיפורי המשתמש, `CLAUDE.md`, והקובץ הזה |
| `ba62efa` | מסמך חדש `docs/architecture.md` (מודולים, דיאגרמת מצבים, טבלת שורות `@@` ופקודות), ו-README מעודכן |
| `5c3cc0d` | מסמך חדש `docs/demo-plan.md` (סדר של 25 דקות, הכנות, גיבויים), Summary ו-Tour בסיפורי המשתמש |
| `31cfb95` | התזכורת בקול ב-README, בסיפור משתמש 6, ב-architecture וב-demo-plan |
| `56ad2cd` | מדריך מהיר לבוחנים בדף אחד: `docs/quick-start.md` ו-PDF |
| `273be3c` | צילום מסך חדש ב-README, ותמונות של Stage view ו-Summary |
| `6b64b11` | דיאגרמת חיבורים צבעונית עם כל הפינים (`docs/images/wiring.png`) |
| `64afc6a` | מקרה קצה 19: טיימר בלי אינטרנט |
| `3dcca38` | העמעום בסיפור משתמש 4, במדריך המהיר וב-README. מקרה קצה 16 עכשיו `Make the light blue` |
| `4f6ecce` | GIF מונפש ב-README: ארבע פקודות קוליות במרכז הבקרה (`docs/images/demo.gif`) |
| `a8f32aa` | שאלה בכתב: מקרה קצה 20, architecture, README, המדריך המהיר ו-demo-plan. PDF-ים מחדש |
| `97ca2ae` | `get_board_status` ב-README, ב-architecture וב-`CLAUDE.md` |
| `ac12619` | Self-test ב-README, ב-demo-plan ובמדריך המהיר, וצילום מסך חדש |
| `e749151` | איך מחברים LDR אמיתי (README, architecture, `CLAUDE.md`) |
| `3e6f258` | תוצאות הקומפילציה בקובץ הזה |
| `0f5c455` | Save report בסיפור משתמש 15, ב-README וב-demo-plan |
| `a720f16` | ב-README: איך מקמפלים עם arduino-cli, וגודל הקושחה הנוכחי (81%) |
| `38cef7d` | שעון ההדגמה ב-demo-plan וב-README |
| `0c58290` | מסמך חדש `docs/versions.md`: כל השלבים 02-30 והשיפורים, בטבלאות |
| `56be7ba` | QR לריפו ב-GitHub במדריך המהיר (גם ב-PDF), כדי שהבוחנים יגיעו לקוד מהדף המודפס |
| הבא אחריו | `TIMEZONE_POSIX` ב-README, ב-`CLAUDE.md` ובבדיקה 5 על הלוח |

## מה צריך לבדוק על הלוח

לא יכולתי לצרוב. אבל **קימפלתי את כל הסקיצה** בענן עם arduino-cli, ESP32 core 2.0.17, WebSockets 2.7.2 ו-ArduinoJson 7.4.3 (עם `config.h` מדומה, לא האמיתי). היא נבנית בלי שגיאות ובלי אזהרות חדשות ב-`-Wall`, גם עם `LIGHT_SENSOR_PIN`:

| | flash | RAM סטטי |
|---|---|---|
| `vad-auto-listen` | 1,042,609 (79%) | 72,896 |
| `improvements` | 1,062,821 (81%) | 75,116 |

כלומר 20KB קוד ו-2.2KB זיכרון קבוע (הבאפרים של הכלים החדשים), אז בזמן ריצה יהיו כ-2KB פחות פנויים. ה-JSON של הסשן נבדק בפרסר. מה שנשאר לבדוק על הלוח:

1. **קומפילציה:** לפתוח את `test/test.ino` ב-Arduino IDE ולעשות Verify. הקבצים `clock_timer_utils.cpp/.h` ו-`board_info_utils.cpp/.h` חדשים ונכנסים לבד.
2. **הדלקה:** כל הנורות ירוקות וברכת הפתיחה נשמעת. הודעת ההגדרות לסשן גדלה מכ-2.4KB לכ-4.9KB (שישה כלים). אם נורית OpenAI לא נהיית ירוקה, זה החשוד הראשון.
3. **אור בקול:** `Dim the light to 30 percent`: סליידר הבהירות זז ל-30% וה-LED מתעמעם. Reset ללוח: הבהירות נשארת. `Make the light blue`: עונה שאי אפשר.
4. **עוצמה בקול:**
   - `Set the volume to 30 percent`: הסליידר זז ל-30% והעוזר מאשר בקול.
   - `A bit louder`: עולה ל-50%.
   - `Set the volume to 150 percent`: נעצר על 100%.
   - מסמנים Mute ואז מבקשים עוצמה בקול: ההשתקה מתבטלת.
   - Reset ללוח: העוצמה נשארת.
5. **שעה:** `What time is it?` עונה בשעה הנכונה.
   - שימו לב: ב-`config.h` מוגדר UTC+2. עד 25.10.2026 ישראל ב-UTC+3. הכי פשוט: להוסיף ל-`config.h` את השורה `#define TIMEZONE_POSIX "IST-2IDT,M3.5.5/2,M10.5.0"` (בלי commit), ואז השעון עובר לבד בין שעון קיץ לחורף (`dd56064`). אפשרות אחרת: לשנות את ההיסט ל-`(3 * 3600)`. זה משפיע גם על ברכת הפתיחה.
6. **טיימר:**
   - `Set a timer for 20 seconds`: במסך ספירה לאחור. אחרי 20 שניות 3 צפצופים וה-LED מהבהב, ואז חוזר למצב שהיה.
   - עם Hands-free דלוק: הצפצופים לא מתחילים הקלטה.
   - טיימר שנגמר באמצע תשובה מצלצל רק אחרי שהתשובה נגמרת.
   - `How long is left on my timer?` באמצע ספירה: עונה בערך כמו המסך.
   - `Cancel the timer`: הספירה נעלמת.
   - טיימר של 30 שניות ואז לכבות את ה-hotspot: הלוח עדיין מצפצף בזמן (בלי הודעה בקול).
   - `Remind me in 30 seconds to check the oven`: בספירה כתוב `check the oven`. בסוף, אחרי הצפצופים, העוזר אומר `Time's up - check the oven` ובמסך כתוב Saying that the timer is done.
   - עם Hands-free דלוק: ההודעה בקול לא מתחילה הקלטה.
7. **`What can you do?`:** עונה במשפט אחד עם הדברים שהוא יודע לעשות.
8. **מצב הלוח:** `How are you doing?`: עונה עם עוצמת ה-Wi-Fi, כמה דקות הוא דלוק וכמה זיכרון פנוי. בשיחה מופיעה תגית 📶.
9. **שאלה בכתב:** בשדה שמתחת ל-Talk לכתוב `What is the capital of Italy?` ו-Ask: העוזר עונה בקול, ובבועה כתוב typed. `Turn on the light` בכתב: ה-LED נדלק. Ask באמצע תשובה: מתחת לשיחה כתוב שהשאלה לא נשלחה.
10. **שלא נשבר כלום:** `Turn on the light`, `Is it day or night?`, Hands-free, הקלטות ב-SD. סליידר האור עדיין משנה את הערך (בלי `LIGHT_SENSOR_PIN`).
11. **רק אם מחברים LDR:** להוסיף `#define LIGHT_SENSOR_PIN 34` ל-`config.h` (בלי commit). סליידר האור במסך זז לבד כשמכסים את החיישן, ו-`Is it day or night?` עונה לפי האור האמיתי.

אם משהו מהקושחה לא עובד, אפשר לבטל רק אותו: `git revert ff777f2` (LDR), `git revert 7bf4cea` (מצב הלוח), `git revert 20de4c7` (שאלה בכתב, מבטל גם את השדה ב-GUI), `git revert 9b6db09` (עמעום בקול), `git revert 22ca52c` (צלצול בלי חיבור), `git revert d9af02e` (כמה זמן נשאר), `git revert 348a78d` (התזכורת בקול), `git revert e0a8753` (טיימר ושעה, אחרי שביטלתם את שני אלה) או `git revert 80b1506` (עוצמה). ה-GUI ממשיך לעבוד גם בלי הכלים האלה.

## בדיקה של מרכז הבקרה

את ה-GUI בדקתי בדפדפן (Chromium) עם שורות הסטטוס האמיתיות מהלוג של 15.10, במצב בהיר וכהה. כדאי לעבור עליו פעם אחת עם הלוח: Connect, שאלה, Hands-free, טיימר, ניתוק כבל ה-USB, Demo checklist ו-Export CSV.

- **Stage view:** ללחוץ F (או הכפתור) באמצע שיחה. רווח צריך עדיין להתחיל הקלטה, ו-Esc סוגר.
- **Summary:** אחרי כמה שאלות, כפתור Summary בחלונית Statistics. המספרים צריכים להתאים לחלונית.
- **Tour:** כפתור Tour למעלה, לעבור על 9 התחנות עם החצים. אחרי Connect גם מתג ה-Hands-free מסומן.
- **Save report:** אחרי כמה שאלות, Summary ואז Save report. נפתח קובץ HTML עם הסיכום, הגרף, טבלה וכל השיחה.
- **Self-test:** אחרי Connect, כפתור Self-test למעלה מימין. ה-LED מהבהב פעם אחת וחוזר למצב שהיה, נשמע צפצוף, ובסוף כתוב all passed.
- **צ'ק-ליסט אוטומטי:** אחרי Connect ושאלה אחת, Demo checklist מראה כמה פריטים מסומנים (גם על הכפתור), ולידם כתוב "אוטומטי".
- **Save בשיחה:** אחרי כמה שאלות, Save מוריד קובץ `.txt` עם הבועות, זמני התגובה וההערות.
- **Replay a log:** בלי לוח מחובר, Replay a log… בטרמינל ולבחור לוג ששמרתם (למשל `docs/measurements/esp32_log_2026-10-15.txt`). השיחה, המצב והסטטיסטיקה מתמלאים. חיבור לוח עוצר את הניגון.

## מסמכים שהשתנו

`README.md`, `docs/user-stories.md`, `docs/edge-cases.md` וה-PDF-ים שלהם השתנו, ונוספו `docs/images/demo.gif`, `docs/images/wiring.png`, `docs/architecture.md`, `docs/demo-plan.md` ו-`docs/quick-start.md` (עם PDF). העותקים בתיקייה "מסמכים נלווים להגשת פרוייקט" בשולחן העבודה לא עודכנו, וצריך לסנכרן אותם.

## עוד לא נעשה

- שמות הצוות ב-README ("להשלים").
- חיישן ה-LDR עוד לא מחובר פיזית. הקוד מוכן ומחכה ל-`LIGHT_SENSOR_PIN`.

## פקודות

<div dir="ltr">

```
git log --oneline vad-auto-listen..improvements     # the night's commits
git diff vad-auto-listen..improvements --stat       # what changed
git diff vad-auto-listen..improvements -- test/     # just the firmware

git push -u origin improvements                     # push the branch as is

git checkout vad-auto-listen                        # or merge it in
git merge --ff-only improvements
git push origin vad-auto-listen
```

</div>

</div>
