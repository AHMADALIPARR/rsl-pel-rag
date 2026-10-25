; loop: sum 1..10 = 55.
; Hand-authored Milestone 002 source for the Milestone 001 loop
; fixture. Expected final state: R1 = 55. The BR uses a backward
; label reference to the loop head (forward references are
; exercised by callret.asm).
MOV R1, R0, 0
MOV R2, R0, 1
MOV R3, R0, 11
MOV R4, R0, 1
loop:
ADD R1, R1, R2
ADD R2, R2, R4
CMP R2, R3
BR NE, loop
RET
