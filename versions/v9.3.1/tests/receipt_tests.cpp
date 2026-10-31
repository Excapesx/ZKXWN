#include <iostream>
#include "rmtc9/receipt_audit.hpp"
int main(){int failed=0;auto check=[&](bool ok){if(!ok)++failed;};rmtc9::ReceiptAudit a;
check(!a.observe("s",0,-1).missingReceipt);a.sent(0,"move");
check(a.observe("s",1,0,"wait").missingReceipt);
a.sent(1,"defend");check(!a.observe("s",2,1,"defend").missingReceipt);
check(a.observe("next",0,-1).expectedTick==-1);
std::cout<<"receipt audit failures="<<failed<<'\n';return failed?1:0;}
