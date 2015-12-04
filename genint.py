#!/usr/bin/python

GPRNames = [
    "r15", "r14", "r13", "r12", "r11", "r10", "r9", "r8", "rax", "rbx",
    "rcx", "rdx", "rbp", "rdi", "rsi"
]
Width = 8
Offset = { name : (pos * Width) for pos, name in enumerate(GPRNames) }

def export_name(name):
    print "\t.global {}".format(name)

def create_entry(label, intno, target):
    errcode = {8, 10, 11, 12, 13, 14, 17, 30}
    label = "{}{}".format(label, intno)
    woerr = "\n\t".join(["{}:", "pushq $0", "pushq ${}", "jmp {}"])
    werr = "\n\t".join(["{}:", "pushq ${}", "jmp {}"])
    temp = [woerr, werr]

    print temp[intno in errcode].format(label, intno, target)
    return label

def leave():
    print "\taddq ${}, %rsp".format((2 + len(GPRNames)) * Width)
    print "\tiretq"

def save_state():
    print "\tsubq ${}, %rsp".format(len(GPRNames) * Width)
    for name in GPRNames:
        print "\tmovq %{}, {}(%rsp)".format(name, Offset[name])

def restore_state():
    for name in GPRNames:
        print "\tmovq {}(%rsp), %{}".format(Offset[name], name)

def create_common(label, target):
    print "{}:".format(label)
    save_state()
    print "\tmovq %rsp, %rdi"
    print "\tcall {}".format(target)
    restore_state()
    leave()

def create_export_table(label, names):
    print "{}:".format(label)
    for name in names:
        print "\t.quad {}".format(name)

print "\t.text"
print "\t.code64"

common, handler, isr_table = "common_isr", "isr_common_handler", "isr_entry"
create_common(common, handler)
names = [ create_entry("entry", intno, common) for intno in xrange(128) ]
export_name(isr_table)
create_export_table(isr_table, names)
