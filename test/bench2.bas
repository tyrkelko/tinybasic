10 T=MILLIS(1): FOR I=1 TO 10000: A=5: NEXT: T=MILLIS(1)-T
20 S=MILLIS(1): FOR I=1 TO 10000: A=5*6: NEXT: S=MILLIS(1)-S
30 U=MILLIS(1): FOR I=1 TO 10000: : NEXT: U=MILLIS(1)-U
40 V=MILLIS(1): FOR I=1 TO 10000: NEXT: V=MILLIS(1)-V
50 PRINT "Loop time", V/10
60 PRINT "Token time", (U-V)/10
70 PRINT "Assignment time", (T-U)/10
80 PRINT "Multiplication time", (S-T)/10
