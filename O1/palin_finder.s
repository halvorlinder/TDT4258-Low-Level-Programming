.global _start

.section .text

_start:
	ldr r0, =input //get the input adress 
	bl transform_input //get it on a nice format
	bl check_input //check if palindrome
	cmp r0, #0
	beq False_pal_check
		bl palindrome_found
		b End_pal_check
	False_pal_check:
		bl palindrome_not_found
	End_pal_check:
	b exit

transform_input: 
    // assumes adress in r0, and removes spaces from the input and converts to upper case
	push {r4,r5} // do not change these globally
	mov r1, #0 //offset 
	Outer_strip_loop:
		ldrb r2, [r0, r1] //load the next char
		cmp r2, #0 // check termination 
		beq Transform_end // done
        cmp r2, #32 // check space 
            bne Outer_strip_end // if not keep iterating
        mov r3, r1 // offset from offset
        Inner_strip_loop: 
            // move all chars to the left
            add r4, r3, #1
            ldrb r5, [r0,r3] // check termination
            cmp r5, #0 
            beq Outer_strip_end // done with shift
            ldrb r5, [r0,r4] // get next
            strb r5, [r0, r3] // store to the left
            mov r3, r4 //inc
            b Inner_strip_loop // redo
    Outer_strip_end: 
        // done with pass
        ldrb r2, [r0,r1] // get char
        cmp r2, #97 // check lower 
        subge r2, r2, #32 //make upper
        strb r2, [r0, r1] // store back 
        add r1, r1, #1 // inc
        b Outer_strip_loop // next pass
	Transform_end:
	pop {r4,r5} // restore
	bx lr
	

check_input:
	// You could use this symbol to check for your input length
	// you can assume that your input string is at least 2 characters 
	// long and ends with a null byte
	
	
check_palindrome:
	// Here you could check whether input is a palindrome or not
    // assumes r0 contains word adress and returns 0 for false or 1 for true to r0
	push {r4,r5} 
    mov r1, #0 // counter
    Find_length_loop:
        // find the length of the word
        ldrb r2, [r0,r1] //get char
        add r1, r1, #1
        cmp r2, #0 // check for null
        bne Find_length_loop // repeat
    sub r1, r1, #2 // index of last char
    cmp r1, #0
    ble Set_false // word is too short
    mov r3, #0 // start counter, while r1 is end counter
    Compare_loop:
        // compare pairwise 
        ldrb r4, [r0,r3] //get left
        ldrb r5, [r0,r1] //get right
        cmp r4, r5
            bne Set_false // not a palindrome
        add r3, r3, #1 // move counters
        sub r1, r1, #1
        cmp r3, r1 // check if done 
            bgt Set_true 
        b Compare_loop //repeat
    Set_false:
        mov r0, #0 // set false
        b Check_end
    Set_true:
        mov r0, #1 // set true 
        b Check_end
    Check_end:
	pop {r4,r5}
	bx lr
	
	
palindrome_found:
	// Switch on only the 5 rightmost LEDs
	// Write 'Palindrome detected' to UART
	push {r4,r5}
        ldr r4, =led_addr
        ldr r4, [r4] // adress of leds 
        mov r5, #31 // rightmost 5
        str r5, [r4]
        ldr r0, = true_message // get message
        push {lr}
        bl print // print it
        pop {lr}
	pop {r4,r5}
	bx lr
	
	
palindrome_not_found:
	// Switch on only the 5 leftmost LEDs
	// Write 'Not a palindrome' to UART
	push {r4,r5}
        ldr r4, =led_addr
        ldr r4, [r4] // adress of leds
        mov r5, #992 //leftmost 5
        str r5, [r4]
        ldr r0, = false_message // get message
        push {lr}
        bl print // print it
        pop {lr}
	pop {r4,r5}
	bx lr

print:
	push {r4,r5}
        ldr r4, =jag_addr
        ldr r4, [r4] // get output adress 
        mov r1, #0 // counter
        Print_loop:
            ldrb r2, [r0,r1] // get char to print
            strb r2, [r4] // write it
            add r1, r1, #1
            cmp r2, #0 // check done
                bne Print_loop
	pop {r4,r5}
	bx lr
	
	
exit:
	// Branch here for exit
	b exit
	

.section .data
.align
	// This is the input you are supposed to check for a palindrome
	// You can modify the string during development, however you
	// are not allowed to change the label 'input'!
    led_addr: .word 0xFF200000
    jag_addr: .word 0xFF201000
	false_message: .asciz "Not a palindrome"
	true_message: .asciz "Palindrome detected"
	input: .asciz "level"
	// input: .asciz "8448"
    // input: .asciz "KayAk"
    // input: .asciz "step on no pets"
    // input: .asciz "Never odd or even"


.end