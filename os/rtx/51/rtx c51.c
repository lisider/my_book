/*************************************************************************************/
/** Author:linger                                                                   **/
/** Email:ling_re@sina.com                                                          **/
/** This file is part of the 'RTX-51' Real-Time Operating System Source Package     **/
/*************************************************************************************/

#include <reg51.H>
#include <absacc.h>

#define  INT_REGBANK        1        /* 定时中断的寄存器组,必须大于0 */
#define  TIMESHARING        5        /* 每个任务的最大运行时间 */
#define  RTX_STACKFREE      20       /* 当前任务的最小堆栈空间 */
#define  RTX_MAXTASKN       10       /* 最大任务数 */
#define  RTX_RAMTOP         0xFF     /* 最大 RAM 数  */
#define  INT_CLOCK          10000    /* 每个定时中断的时钟数 */

#define  RTX_TIMESHARING    (0 - TIMESHARING)
#define  RTX_CLOCK          (0 - INT_CLOCK)
#define  RTX_REGISTERBANK   INT_REGBANK

#define  K_SIG          1
#define  K_TMO          2
#define  SIG_EVENT      4
#define  TMO_EVENT      8
#define  K_READY        16
#define  K_ACTIVE       32
#define  K_ROBIN        64
#define  K_IVL          128

#define  B_WAITSIG      0
#define  B_WAITTIM      1
#define  B_SIGNAL       2
#define  B_TIMEOUT      3
#define  B_READY        4
#define  B_ACTIVE       5
#define  B_ROBIN        6
#define  B_INTERVAL     7

idata unsigned char   STKP[RTX_MAXTASKN];   /* 指向前一任务堆栈的尾地址 */
idata unsigned char   RTX_RobinTime;        /* 每个任务最长的运行周期 */
idata unsigned char   TASK_Current;         /* 当前运行的任务号 */
idata unsigned char   RTX_SAVEPSW;
idata unsigned char   RTX_SAVEACC;

bit   RTX_TS_REQ=0;
bit   RTX_TS_DELAY=0;

idata struct
{
   char  time;
   char  st;
}
STATE[RTX_MAXTASKN];

void os_system_init();
unsigned char task_switch();
unsigned char os_system_start();
unsigned char os_delete_task(unsigned char task_no);
unsigned char os_send_signal(unsigned char task_no);
unsigned char isr_send_signal(unsigned char task_no);
unsigned char os_clear_signal(unsigned char task_no);
unsigned char os_wait(unsigned type, unsigned timeout);
unsigned char os_create_task(unsigned int proc_name, unsigned char task_no);

/*******************************************************************/
unsigned char task_switch()
{
     unsigned char i;
     unsigned char next;
     unsigned char limit;

     SP-=2;

     RTX_TS_DELAY=1;

     next=TASK_Current;

     while(1)
     {
        if (++next==RTX_MAXTASKN)next=0;
        if (STATE[next].st&K_READY)break;
     }

     while(TASK_Current<next)
     {
        TASK_Current++;
        i=STKP[TASK_Current];

        STKP[TASK_Current]=SP;

        if(TASK_Current==RTX_MAXTASKN)
          {
             limit=RTX_RAMTOP;
          }
        else
          {
             limit=STKP[TASK_Current+1];
          }

        while(i!=limit)
        {
           SP++;
           i++;
           DBYTE[SP]=DBYTE[i];
        }
     }

     while(TASK_Current>next)
     {
        if(TASK_Current==RTX_MAXTASKN)
          {
             i=RTX_RAMTOP;
          }
        else
          {
             i = STKP[TASK_Current+1];
          }

        limit = STKP[TASK_Current];

        while(SP!=limit)
        {
           DBYTE[i]=DBYTE[SP];
           i--;
           SP--;
        }
        STKP[TASK_Current] = i;

        TASK_Current--;
      }


      RTX_RobinTime = STATE[TASK_Current].time + RTX_TIMESHARING;
      EA=0;
      if(STATE[TASK_Current].st & K_ROBIN)
        {
           EA=1;
           DBYTE[0]=DBYTE[SP];
           SP--;
           DBYTE[1]=DBYTE[SP];
           SP--;
           DBYTE[2]=DBYTE[SP];
           SP--;
           DBYTE[3]=DBYTE[SP];
           SP--;
           DBYTE[4]=DBYTE[SP];
           SP--;
           DBYTE[5]=DBYTE[SP];
           SP--;
           DBYTE[6]=DBYTE[SP];
           SP--;
           DBYTE[7]=DBYTE[SP];
           SP--;

           DPL=DBYTE[SP];
           SP--;
           DPH=DBYTE[SP];
           SP--;
           B=DBYTE[SP];
           SP--;
           PSW=DBYTE[SP];
           SP--;
           ACC=DBYTE[SP];
           SP--;

           RTX_TS_DELAY=0;
           RTX_TS_REQ=0;
           return(0x00);
        }

      if((STATE[TASK_Current].st & K_SIG) && (STATE[TASK_Current].st & SIG_EVENT))
        {
           STATE[TASK_Current].st&=0xf0;
           EA=1;
           RTX_TS_DELAY=0;
           RTX_TS_REQ=0;
           return(SIG_EVENT);
        }

      if((STATE[TASK_Current].st & K_TMO) && (STATE[TASK_Current].st & TMO_EVENT))
        {
           STATE[TASK_Current].st&=0xf4;
           EA=1;
           RTX_TS_DELAY=0;
           RTX_TS_REQ=0;
           return(TMO_EVENT);
        }

      EA=1;
      RTX_TS_DELAY=0;
      RTX_TS_REQ=0;
      return(0x00);
}

void timer0_comm()
{
     unsigned char i;
     unsigned char stack_free;

//Update_Timer0:

     TR0=0;
     TL0+=(RTX_CLOCK+9)%256;
     if(CY)TH0++;
     TH0+=(RTX_CLOCK+9)/256;
     TR0=1;


//Chcec_Stack:   /* 堆栈检查，如果剩余堆栈 <  RTX_STACKFREE 转去错误处理程序 */

     stack_free=TASK_Current==RTX_MAXTASKN ? RTX_RAMTOP : STKP[TASK_Current+1];

     stack_free=stack_free-SP;

     if(stack_free<RTX_STACKFREE)
       {
           EA=0;
           while(1)
           {
              /* ************此处加入堆栈溢出处理程序*************** */
           };
       }


//Update_Check_Task_Timers:

      for(i=0;i<RTX_MAXTASKN;i++)
         {
             STATE[i].time--;
             EA=0;
             if((STATE[i].st&K_TMO)&&(STATE[i].time==0))STATE[i].st|=K_READY+TMO_EVENT;
             EA=1;
         }

//Check_Round_Robin_TimeOut:

      ACC=RTX_SAVEACC;
      PSW=RTX_SAVEPSW;

      if(RTX_TIMESHARING==0)     /* 没有任务切换 */
        {
            return;
        }

      if(STATE[TASK_Current].time!=RTX_RobinTime) /* 没有任务切换 */
        {
            return;
        }

      if(RTX_TS_DELAY)
        {
           RTX_TS_REQ=1;
           return;
        }

     SP++;
     DBYTE[SP]=ACC;
     SP++;
     DBYTE[SP]=PSW;
     SP++;
     DBYTE[SP]=B;
     SP++;
     DBYTE[SP]=DPH;
     SP++;
     DBYTE[SP]=DPL;
     SP++;
     DBYTE[SP]=DBYTE[0];
     SP++;
     DBYTE[SP]=DBYTE[1];
     SP++;
     DBYTE[SP]=DBYTE[2];
     SP++;
     DBYTE[SP]=DBYTE[3];
     SP++;
     DBYTE[SP]=DBYTE[4];
     SP++;
     DBYTE[SP]=DBYTE[5];
     SP++;
     DBYTE[SP]=DBYTE[6];
     SP++;
     DBYTE[SP]=DBYTE[6];

     EA=0;
     STATE[TASK_Current].st|=K_ROBIN;
     EA=1;

     task_switch();
}


void timer0_int() interrupt 1 using RTX_REGISTERBANK
{
     union
     {
        char tmp[2];
        unsigned int temp;
     }
     RTX_PROCE;

     EA=0;

     RTX_SAVEACC=ACC;
     RTX_SAVEPSW=DBYTE[SP];

     RTX_PROCE.temp=timer0_comm;

     SP--;
     DBYTE[SP]=RTX_PROCE.tmp[1];
     SP++;
     DBYTE[SP]=RTX_PROCE.tmp[0];

     SP++;
     DBYTE[SP]=RTX_SAVEACC;

     SP++;
     DBYTE[SP]=RTX_SAVEPSW;

     EA=1;
}

unsigned char os_wait(unsigned type, unsigned timeout)
{
   unsigned char st = 0;

   if(type==0)
     {
        EA=0;
        STATE[TASK_Current].st &= ~ (st | K_SIG | K_TMO);
        EA=1;
        ET0=1;
        return (st);
     }

   ET0=0;

   if(type&K_IVL)
     {
        STATE[TASK_Current].time+=timeout;
        if(!CY)
          {
             st = TMO_EVENT;
              EA=0;
              STATE[TASK_Current].st &= ~ (st | K_SIG | K_TMO);
              EA=1;
              ET0=1;
              return (st);
          }
        EA=0;
        STATE[TASK_Current].st |= K_TMO;
        EA=1;
      }

    if(type&K_TMO)
      {
         if(timeout==0)
           {
              st = TMO_EVENT;
              EA=0;
              STATE[TASK_Current].st &= ~ (st | K_SIG | K_TMO);
              EA=1;
              ET0=1;
              return (st);
           }
         STATE[TASK_Current].time = timeout;

         EA=0;
         STATE[TASK_Current].st |= K_TMO;
         EA=1;
      }

    if(type&K_SIG)
      {
         if(STATE[TASK_Current].st&SIG_EVENT)
           {
              st=SIG_EVENT;
              EA=0;
              STATE[TASK_Current].st &= ~ (st | K_SIG | K_TMO);
              EA=1;
              ET0=1;
              return (st);
           }
         EA=0;
         STATE[TASK_Current].st |= K_SIG;
         EA=1;
    }

    EA=0;
    STATE[TASK_Current].st &= ~K_READY;
    EA=1;
    ET0=1;
    task_switch();
}

unsigned char os_clear_signal(unsigned char task_no)
{
   data unsigned char *p;
   if (task_no>RTX_MAXTASKN)return(0xff);
   EA=0;
   p=&STATE[task_no].st;
   *p&=SIG_EVENT;
   EA=1;
   return(0);
}


unsigned char os_send_signal(unsigned char task_no)
{
   data unsigned char *p;

   if (task_no>RTX_MAXTASKN)  return (0xff);

   EA=0;
   p=&STATE[task_no].st;

   if(*p&K_ACTIVE)
     {
        if (*p&K_SIG)*p|=K_READY;
     }
   *p|=SIG_EVENT;

   EA=1;

   return(0);
}

unsigned char isr_send_signal(unsigned char task_no)
{
   unsigned char *p;

   if (task_no>RTX_MAXTASKN)  return (0xff);

   EA=0;
   p=&STATE[task_no].st;

   if(*p&K_ACTIVE)
     {
        if (*p&K_SIG)*p|=K_READY;
     }
   *p|=SIG_EVENT;

   EA=1;

   return(0);
}

unsigned char os_create_task(unsigned int proc_name, unsigned char task_no)
{
   unsigned char i,j;
   unsigned char p1,p2;

   union
   {
      char tmp[2];
      unsigned int temp;
   }
   RTX_PROCE;

   RTX_PROCE.temp=proc_name;

   if(task_no>RTX_MAXTASKN)return(0xff);

   if(STATE[task_no].st&K_ACTIVE)return(0xff);

   STATE[task_no].st|=K_ACTIVE+K_READY;

   i=TASK_Current;

   while(i<task_no)
   {
      i++;
      p1=STKP[i];
      p2=i==RTX_MAXTASKN ? RTX_RAMTOP : STKP[i+1];

      while(p1!=p2)
      {
         p1++;
	 j=p1-2;
	 DBYTE[j]=DBYTE[p1];
      }
      STKP[i]-=2;
   }

   if(i>task_no)SP+=2;

   while(i>task_no)
   {
      p1=i==TASK_Current ? SP : STKP[i+1];
      STKP[i]+=2;
      p2=STKP[i];
      while(p1!=p2)
      {
         j=p1-2;
         DBYTE[p1]=DBYTE[j];
	 p1--;
      }
      i--;
   }

   DBYTE[STKP[task_no]+1]=RTX_PROCE.tmp[1];
   DBYTE[STKP[task_no]+2]=RTX_PROCE.tmp[0];

   return(0);
}


unsigned char os_delete_task(unsigned char task_no)
{
    unsigned char first,last,check;

    if(task_no>RTX_MAXTASKN)return(0xff);

    if(!(STATE[task_no].st & K_ACTIVE))return(0xff);

    EA=0;
    STATE[task_no].st &= ~(K_ACTIVE | K_READY | K_SIG | K_TMO | K_ROBIN);
    EA=1;

    if(TASK_Current==task_no)
      {
         SP=STKP[task_no];
         task_switch();
      }

    if(TASK_Current<task_no)
      {
         last=task_no==RTX_MAXTASKN ? RTX_RAMTOP : STKP[task_no+1];
         first=STKP[task_no];
         do{
              check=STKP[task_no];
              while(first!=check)
              {
                 DBYTE[last]=DBYTE[first];
                 last--;
                 first--;
              }
              STKP[task_no]=last;
              task_no--;
           }while (TASK_Current!=task_no);
         return (0);
      }

    if(TASK_Current>task_no)
      {
         last=STKP[task_no+1];
         first=STKP[task_no];
         do{
              task_no++;
              STKP[task_no]=first;
              check=(task_no==TASK_Current) ? SP : STKP[task_no+1];
              while(last!=check)
              {
                 last++;
                 first++;
                 DBYTE[first]=DBYTE[last];
              }
           }while(TASK_Current!=task_no);
         SP=first;
         return (0);
      }
}

void os_system_init()  /* 操作系统初始化 */
{
     unsigned char   i;

     EA=0;

     for(i=0;i<RTX_MAXTASKN;i++)
        {
	   STKP[i]=RTX_RAMTOP;
	   STATE[i].st=0;
	}

     STKP[0]=SP;
     TASK_Current=0;
}


unsigned char os_system_start()  /* 操作系统初始化 */
{
     unsigned char i;
     unsigned char next;
     unsigned char limit;

     EA=0;

     RTX_TS_REQ=0;

     RTX_TS_DELAY=0;

     TASK_Current=0;

     next=TASK_Current;

     while(1)
     {
        if ((++next)==(RTX_MAXTASKN+1))next=0;
        if (STATE[next].st&K_READY)break;
     }

     while(TASK_Current<next)
     {
        TASK_Current++;
        i=STKP[TASK_Current];

        STKP[TASK_Current]=SP;

        if(TASK_Current==RTX_MAXTASKN)
          {
             limit=RTX_RAMTOP;
          }
        else
          {
             limit=STKP[TASK_Current+1];
          }

        while(i!=limit)
        {
           SP++;
           i++;
           DBYTE[SP]=DBYTE[i];
        }
     }

     while(TASK_Current>next)
     {
        if(TASK_Current==RTX_MAXTASKN)
          {
             i=RTX_RAMTOP;
          }
        else
          {
             i = STKP[TASK_Current+1];
          }

        limit = STKP[TASK_Current];

        while(SP!=limit)
        {
           DBYTE[i]=DBYTE[SP];
           i--;
           SP--;
        }
        STKP[TASK_Current] = i;

        TASK_Current--;
      }

      RTX_RobinTime=RTX_TIMESHARING;

      TMOD|=0X01;
      TH0=(RTX_CLOCK)/256;
      TL0=(RTX_CLOCK)%256;

      ET0=1;
      TR0=1;
      EA=1;

      return(0x00);
}

/*******************************************************************/

void task_test1()      /* 测试任务1 */
{
     char i;

     while(1)
     {
        i++;
        i++;
        i++;
        i++;
     };
}

void task_test2()      /* 测试任务2 */
{
     char i;

     while(1)
     {
        i++;
        i++;
        i++;
        i++;
     };
}

void task_test3()      /* 测试任务3 */
{
     char i;

     while(1)
     {
        i++;
        i++;
        i++;
        i++;
     };
}

void main()
{
     os_system_init();

     os_create_task(task_test1,3);
     os_create_task(task_test2,4);
     os_create_task(task_test3,5);

     os_system_start();
}










