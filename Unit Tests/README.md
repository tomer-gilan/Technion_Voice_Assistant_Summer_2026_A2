<div dir="rtl">

# בדיקות יחידה לחומרה (HW Unit Tests)

קבוצה A2, קורס IoT, קיץ 2026. כל בדיקה היא sketch עצמאי שבודק רכיב אחד בלי שום רשת או ענן, כדי שתקלה בחומרה תתגלה לבד ובשיטת אלימינציה. כל sketch מחזיק עותק של הפינים שלו (אותם פינים כמו ב-`ESP32/parameters.h`), ולכן אפשר לפתוח אותו ישירות ב-Arduino IDE ולצרוב.

**איך מריצים:** פותחים את התיקייה של הבדיקה ב-Arduino IDE (‏ESP32 Dev Module), צורבים, ופותחים Serial Monitor ב-115200. בדיקה אוטומטית מדפיסה `===== ... PASSED =====` או `FAILED` עם הסיבה. בדיקה שצריך לראות או לשמוע אומרת בדיוק למה לשים לב.

כולן נכתבו ל-ESP32 core 2.0.17. 

## הבדיקות

<table dir="rtl">
<thead>
<tr><th align="center">#</th><th>רכיב</th><th>מה בודקים</th><th>איך יודעים שעבר</th><th>תוצאה</th></tr>
</thead>
<tbody>
<tr><td align="center">02</td><td>רמקול + מגבר MAX98357A</td><td>צליל 440Hz למשך שנייה, דרך I2S</td><td><code>SPEAKER TEST DONE - PASS if you heard a clean, steady 440 Hz tone</code> ושומעים צליל נקי ויציב. <code>SPEAKER TEST FAILED</code> אם הדרייבר לא עלה</td><td>✅ עבר (שלב 2). עודכנה אחר כך: בלי פין MCLK ועם שורת תוצאה</td></tr>
<tr><td align="center">03</td><td>מיקרופון INMP441</td><td>מינימום, מקסימום ו-RMS כל חצי שנייה</td><td><code>MIC TEST PASSED</code> כשה-RMS בדיבור גבוה לפחות פי 2 מהחלון השקט ביותר, תוך 15 שניות. <code>MIC TEST FAILED</code> עם הסיבה (אפסים בלבד, או שהרמה לא עלתה)</td><td>✅ עבר (שלב 3). גילינו שהערוץ הוא <code>ONLY_RIGHT</code> (‏<code>ONLY_LEFT</code> קורא אפסים), ושהמיקרופון שקט, ולכן יש הגברה בתוכנה. עודכנה אחר כך: בלי פין MCLK ועם שורת תוצאה</td></tr>
<tr><td align="center">04</td><td>מיקרופון ורמקול יחד</td><td>מקליט 2 שניות (LED דולק) ומשמיע (LED כבוי), שוב ושוב, על פורט I2S אחד משותף</td><td><code>LOOPBACK TEST DONE - PASS if you heard your own voice played back clearly</code> ושומעים את עצמכם חוזרים. <code>LOOPBACK TEST FAILED</code> אם ההקלטה שקטה לגמרי</td><td>✅ עבר (שלב 4). עודכנה אחר כך: בלי פין MCLK ועם שורת תוצאה</td></tr>
<tr><td align="center">05</td><td>Wi-Fi</td><td>התחברות, כתובת IP, וחיבור מחדש כשהרשת נופלת</td><td><code>WIFI TEST PASSED</code> אחרי שמודפס <code>IP address</code>, וחיבור מחדש אחרי כיבוי והדלקה של ה-hotspot. <code>WIFI TEST FAILED</code> אם אין חיבור תוך 20 שניות</td><td>✅ עבר (שלב 5). צריך למלא שם וסיסמה בראש הקובץ. עודכנה אחר כך: timeout של 20 שניות ושורת תוצאה</td></tr>
<tr><td align="center">06</td><td>כרטיס SD (SPI)</td><td>חיבור, סוג וגודל, כתיבה, קריאה, השוואה ומחיקה של קובץ</td><td><code>SD CARD TEST PASSED</code></td><td>✅ עבר (גרסת ההגשה). אותה בדיקה רצה בקושחה בכל הדלקה מאז שלב 24</td></tr>
<tr><td align="center">07</td><td>LED (‏PWM)</td><td>3 עמעומים מלאים, ואז 100%, 30% וכבוי</td><td>העמעום חלק, ו-30% נראה עמום יותר מ-100%. עקומת הבהירות ריבועית, כמו בקושחה</td><td>✅ עבר (גרסת ההגשה). עקומת הבהירות עודכנה אחר כך להתאים לקושחה (ריבועית)</td></tr>
<tr><td align="center">08</td><td>כפתור BOOT (‏GPIO0)</td><td>debounce של saturating counter, וספירה של כל שינוי גולמי ברמה</td><td><code>BOOT BUTTON TEST PASSED</code> אחרי 5 לחיצות, ו-0 שינויים גולמיים כשלא נוגעים</td><td>✅ עבר (גרסת ההגשה)</td></tr>
<tr><td align="center">09</td><td>חיישן אור LDR (‏ADC1, GPIO34)</td><td>שני שלבים מודרכים: קריאה ממוצעת כשהחיישן מכוסה, ואז עם פנס</td><td><code>LIGHT SENSOR TEST PASSED</code> כשהקריאה עם הפנס גבוהה בלפחות 1500 מהקריאה כשהוא מכוסה. אם כיסוי נותן מספר <strong>גבוה</strong> יותר, ‏<code>FAILED</code> מורה להחליף בין ה-LDR לנגד</td><td>⏸ ה-LDR לא מחובר, לפי הנחיית המרצה. ערך האור מדומה במרכז הבקרה. הבדיקה מוכנה לחיבור ה-LDR, וההוראות ב-<a href="../Documentation/calibration.md">calibration.md</a></td></tr>
</tbody>
</table>


</div>
