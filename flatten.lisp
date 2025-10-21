;; FLATTEN - Flatten a list by one level
;; Example: ((A) B C) => (A B C)
;; Style matches firstatom.lisp with explicit function binding

((LAMBDA (FLATTEN X) (FLATTEN X))
 (QUOTE (LAMBDA (X)
          (COND ((EQ X ()) ())
                ((ATOM (CAR X))
                 (CONS (CAR X) (FLATTEN (CDR X))))
                ((QUOTE T)
                 (CONS (CAR (CAR X)) (FLATTEN (CDR X)))))))
 (QUOTE ((A) B C)))
