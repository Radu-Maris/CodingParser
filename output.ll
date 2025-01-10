define i32 @main() {
entry:
  %addtmp = add i32 1, 1
  %ifcond = icmp ne i32 %addtmp, 0
  br i1 %ifcond, label %while.body, label %while.end

while.body:                                       ; preds = %entry
  %addtmp1 = add i32 2, 2
  br label %while.end

while.end:                                        ; preds = %while.body, %entry
  ret i32 0
}
