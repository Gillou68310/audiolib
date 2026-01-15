IDEAL

public _sub_2A110
public _sub_2A1B1
public _sub_2A252
public _sub_2A333

segment SEG025 byte public 'CODE' use16
assume cs:SEG025

proc _sub_2A110 far

to = dword ptr  6
from = dword ptr  0Ah
len = word ptr  0Eh

    push    bp
    mov     bp, sp
    push    si
    push    di
    push    ds
    lds     si, [bp+from]
    les     di, [bp+to]
    mov     dx, [bp+len]
    sub     bx, bx
    cmp     dx, 10h
    jb      short label2

label1:
    mov     al, [bx+si]
    add     [es:bx+di], al
    mov     al, [bx+si+1]
    add     [es:bx+di+1], al
    mov     al, [bx+si+2]
    add     [es:bx+di+2], al
    mov     al, [bx+si+3]
    add     [es:bx+di+3], al
    mov     al, [bx+si+4]
    add     [es:bx+di+4], al
    mov     al, [bx+si+5]
    add     [es:bx+di+5], al
    mov     al, [bx+si+6]
    add     [es:bx+di+6], al
    mov     al, [bx+si+7]
    add     [es:bx+di+7], al
    mov     al, [bx+si+8]
    add     [es:bx+di+8], al
    mov     al, [bx+si+9]
    add     [es:bx+di+9], al
    mov     al, [bx+si+0Ah]
    add     [es:bx+di+0Ah], al
    mov     al, [bx+si+0Bh]
    add     [es:bx+di+0Bh], al
    mov     al, [bx+si+0Ch]
    add     [es:bx+di+0Ch], al
    mov     al, [bx+si+0Dh]
    add     [es:bx+di+0Dh], al
    mov     al, [bx+si+0Eh]
    add     [es:bx+di+0Eh], al
    mov     al, [bx+si+0Fh]
    add     [es:bx+di+0Fh], al
    add     bx, 10h
    sub     dx, 10h
    cmp     dx, 10h
    jnb     short label1

label2:
    or      dx, dx
    jz      short label4

label3:
    mov     al, [bx+si]
    add     [es:bx+di], al
    inc     bx
    dec     dx
    jnz     short label3

label4:
    pop     ds
    pop     di
    pop     si
    pop     bp
    retf
endp _sub_2A110


proc _sub_2A1B1 far

to = dword ptr  6
from = dword ptr  0Ah
len = word ptr  0Eh

    push    bp
    mov     bp, sp
    push    si
    push    di
    push    ds
    lds     si, [bp+from]
    les     di, [bp+to]
    mov     dx, [bp+len]
    sub     bx, bx
    cmp     dx, 10h
    jb      short label6

label5:
    mov     al, [bx+si]
    sub     [es:bx+di], al
    mov     al, [bx+si+1]
    sub     [es:bx+di+1], al
    mov     al, [bx+si+2]
    sub     [es:bx+di+2], al
    mov     al, [bx+si+3]
    sub     [es:bx+di+3], al
    mov     al, [bx+si+4]
    sub     [es:bx+di+4], al
    mov     al, [bx+si+5]
    sub     [es:bx+di+5], al
    mov     al, [bx+si+6]
    sub     [es:bx+di+6], al
    mov     al, [bx+si+7]
    sub     [es:bx+di+7], al
    mov     al, [bx+si+8]
    sub     [es:bx+di+8], al
    mov     al, [bx+si+9]
    sub     [es:bx+di+9], al
    mov     al, [bx+si+0Ah]
    sub     [es:bx+di+0Ah], al
    mov     al, [bx+si+0Bh]
    sub     [es:bx+di+0Bh], al
    mov     al, [bx+si+0Ch]
    sub     [es:bx+di+0Ch], al
    mov     al, [bx+si+0Dh]
    sub     [es:bx+di+0Dh], al
    mov     al, [bx+si+0Eh]
    sub     [es:bx+di+0Eh], al
    mov     al, [bx+si+0Fh]
    sub     [es:bx+di+0Fh], al
    add     bx, 10h
    sub     dx, 10h
    cmp     dx, 10h
    jnb     short label5

label6:
    or      dx, dx
    jz      short label8

label7:
    mov     al, [bx+si]
    sub     [es:bx+di], al
    inc     bx
    dec     dx
    jnz     short label7

label8:
    pop     ds
    pop     di
    pop     si
    pop     bp
    retf
endp _sub_2A1B1


proc _sub_2A252 far

to = dword ptr  6
from = dword ptr  0Ah
len = word ptr  0Eh
shift = word ptr  10h

    push    bp
    mov     bp, sp
    push    si
    push    di
    push    ds
    lds     si, [bp+from]
    les     di, [bp+to]
    mov     dx, [bp+len]
    mov     cx, [bp+shift]
    cmp     dx, 10h
    jnb     short label9
    jmp     label10

label9:
    mov     al, [si]
    cbw
    shl     ax, cl
    add     [es:di], ax
    mov     al, [si+1]
    cbw
    shl     ax, cl
    add     [es:di+2], ax
    mov     al, [si+2]
    cbw
    shl     ax, cl
    add     [es:di+4], ax
    mov     al, [si+3]
    cbw
    shl     ax, cl
    add     [es:di+6], ax
    mov     al, [si+4]
    cbw
    shl     ax, cl
    add     [es:di+8], ax
    mov     al, [si+5]
    cbw
    shl     ax, cl
    add     [es:di+0Ah], ax
    mov     al, [si+6]
    cbw
    shl     ax, cl
    add     [es:di+0Ch], ax
    mov     al, [si+7]
    cbw
    shl     ax, cl
    add     [es:di+0Eh], ax
    mov     al, [si+8]
    cbw
    shl     ax, cl
    add     [es:di+10h], ax
    mov     al, [si+9]
    cbw
    shl     ax, cl
    add     [es:di+12h], ax
    mov     al, [si+0Ah]
    cbw
    shl     ax, cl
    add     [es:di+14h], ax
    mov     al, [si+0Bh]
    cbw
    shl     ax, cl
    add     [es:di+16h], ax
    mov     al, [si+0Ch]
    cbw
    shl     ax, cl
    add     [es:di+18h], ax
    mov     al, [si+0Dh]
    cbw
    shl     ax, cl
    add     [es:di+1Ah], ax
    mov     al, [si+0Eh]
    cbw
    shl     ax, cl
    add     [es:di+1Ch], ax
    mov     al, [si+0Fh]
    cbw
    shl     ax, cl
    add     [es:di+1Eh], ax
    add     si, 10h
    add     di, 20h
    sub     dx, 10h
    cmp     dx, 10h
    jb      short label10
    jmp     label9

label10:
    or      dx, dx
    jz      short label12

label11:
    mov     al, [si]
    cbw
    shl     ax, cl
    add     [es:di], ax
    inc     si
    add     di, 2
    dec     dx
    jnz     short label11

label12:
    pop     ds
    pop     di
    pop     si
    pop     bp
    retf
endp _sub_2A252


proc _sub_2A333 far

to = dword ptr  6
from = dword ptr  0Ah
len = word ptr  0Eh
shift = word ptr  10h

    push    bp
    mov     bp, sp
    push    si
    push    di
    push    ds
    lds     si, [bp+from]
    les     di, [bp+to]
    mov     dx, [bp+len]
    mov     cx, [bp+shift]
    cmp     dx, 10h
    jnb     short label13
    jmp     label14

label13:
    mov     al, [si]
    cbw
    shl     ax, cl
    sub     [es:di], ax
    mov     al, [si+1]
    cbw
    shl     ax, cl
    sub     [es:di+2], ax
    mov     al, [si+2]
    cbw
    shl     ax, cl
    sub     [es:di+4], ax
    mov     al, [si+3]
    cbw
    shl     ax, cl
    sub     [es:di+6], ax
    mov     al, [si+4]
    cbw
    shl     ax, cl
    sub     [es:di+8], ax
    mov     al, [si+5]
    cbw
    shl     ax, cl
    sub     [es:di+0Ah], ax
    mov     al, [si+6]
    cbw
    shl     ax, cl
    sub     [es:di+0Ch], ax
    mov     al, [si+7]
    cbw
    shl     ax, cl
    sub     [es:di+0Eh], ax
    mov     al, [si+8]
    cbw
    shl     ax, cl
    sub     [es:di+10h], ax
    mov     al, [si+9]
    cbw
    shl     ax, cl
    sub     [es:di+12h], ax
    mov     al, [si+0Ah]
    cbw
    shl     ax, cl
    sub     [es:di+14h], ax
    mov     al, [si+0Bh]
    cbw
    shl     ax, cl
    sub     [es:di+16h], ax
    mov     al, [si+0Ch]
    cbw
    shl     ax, cl
    sub     [es:di+18h], ax
    mov     al, [si+0Dh]
    cbw
    shl     ax, cl
    sub     [es:di+1Ah], ax
    mov     al, [si+0Eh]
    cbw
    shl     ax, cl
    sub     [es:di+1Ch], ax
    mov     al, [si+0Fh]
    cbw
    shl     ax, cl
    sub     [es:di+1Eh], ax
    add     si, 10h
    add     di, 20h
    sub     dx, 10h
    cmp     dx, 10h
    jb      short label14
    jmp     label13

label14:
    or      dx, dx
    jz      short label16

label15:
    mov     al, [si]
    cbw
    shl     ax, cl
    sub     [es:di], ax
    inc     si
    add     di, 2
    dec     dx
    jnz     short label15

label16:
    pop     ds
    pop     di
    pop     si
    pop     bp
    retf
endp _sub_2A333

ends SEG025

END