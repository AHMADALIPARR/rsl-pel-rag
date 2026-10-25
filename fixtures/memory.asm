; memory: build 0x1122334455667788, store it, load it back.
; Hand-authored Milestone 002 source for the Milestone 001
; memory fixture. Expected final state: R3 = 0x1122334455667788.
MOV R2, R0, 0x11223344
MOV R4, R0, 32
SHL R2, R2, R4
MOV R5, R0, 0x55667788
OR R2, R2, R5
ALLOC R1, R0, 16
STORE R2, R1, R0, 0
LOAD R3, R1, R0, 0
FREE R1
RET
