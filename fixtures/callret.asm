; callret: 21 doubled by a subroutine = 42.
; Hand-authored Milestone 002 source for the Milestone 001
; callret fixture. Expected final state: R2 = 42, R3 = 42.
; The CALL uses a forward label reference.
MOV R1, R0, 21
CALL double
MOV R3, R2
RET
double:
ADD R2, R1, R1
RET
