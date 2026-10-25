; arith: 40 + 2 = 42; 42 * 10 = 420; 420 - 42 = 378.
; Hand-authored Milestone 002 source for the Milestone 001 arith
; fixture. Expected final state: R3 = 42, R6 = 378.
MOV R1, R0, 40
MOV R2, R0, 2
ADD R3, R1, R2
MOV R4, R0, 10
MUL R5, R3, R4
SUB R6, R5, R3
RET
