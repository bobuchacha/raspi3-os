#define PRINTF_LONG_SUPPORT
#ifndef _VA_LIST
    typedef __builtin_va_list va_list;
    #define _VA_LIST
#endif
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_end(ap)          __builtin_va_end(ap)
#define va_arg(ap, type)    __builtin_va_arg(ap, type)

#ifdef PRINTF_LONG_SUPPORT
static void uli2a(unsigned long long num, unsigned long base, int uc,char * bf)
    {
    register int n=0;
    register unsigned long int d=1;
    while (num/d >= base) {
        d*=base;
    }
        
    while (d!=0) {
        int dgt = num / d;
        num%=d;
        d/=base;
        if (n || dgt>0|| d==0) {
            *bf++ = dgt+(dgt<10 ? '0' : (uc ? 'A' : 'a')-10);
            ++n;
            }
        }
    *bf=0;
    
    }

static void li2a (long num, char * bf)
    {
    if (num<0) {
        num=-num;
        *bf++ = '-';
        }
    uli2a(num,10,0,bf);
    }

#endif

static void ui2a(unsigned int num, unsigned int base, int uc,char * bf)
    {

    int n=0;
    unsigned int d=1;
    while (num/d >= base)
        d*=base;
    while (d!=0) {
        int dgt = num / d;
        num%= d;
        d/=base;
        if (n || dgt>0 || d==0) {
            *bf++ = dgt+(dgt<10 ? '0' : (uc ? 'A' : 'a')-10);
            ++n;
            }
        }
    *bf=0;
    }

static void i2a (int num, char * bf)
    {
    if (num<0) {
        num=-num;
        *bf++ = '-';
        }
    ui2a(num,10,0,bf);
    }

static int a2d(char ch)
    {
    if (ch>='0' && ch<='9')
        return ch-'0';
    else if (ch>='a' && ch<='f')
        return ch-'a'+10;
    else if (ch>='A' && ch<='F')
        return ch-'A'+10;
    else return -1;
    }

static char a2i(char ch, char** src,int base,int* nump)
    {
    char* p= *src;
    int num=0;
    int digit;
    while ((digit=a2d(ch))>=0) {
        if (digit>base) break;
        num=num*base+digit;
        ch=*p++;
        }
    *src=p;
    *nump=num;
    return ch;
    }

static void putchw(char* buff, int* buff_offset, int n, char z, char* bf)
    {
    char fc=z? '0' : ' ';
    char ch;
    char* p=bf;
    while (*p++ && n > 0)
        n--;
    while (n-- > 0)
        buff[(*buff_offset)++] = fc;
    while ((ch= *bf++))
        buff[(*buff_offset)++] = ch;
    }

void fprint(char* buff, char *fmt, ...)
    {
    va_list va;
    va_start(va,fmt);

    char bf[12];
    int buff_offset = 0;
    char ch;

    while ((ch=*(fmt++))) {
        if (ch!='%')
            buff[buff_offset++] = ch;
        else {
            char lz=0;
#ifdef  PRINTF_LONG_SUPPORT
            char lng=0;
#endif
            int w=0;
            ch=*(fmt++);
            if (ch=='0') {
                ch=*(fmt++);
                lz=1;
                }
            if (ch>='0' && ch<='9') {
                ch=a2i(ch,&fmt,10,&w);
                }
#ifdef  PRINTF_LONG_SUPPORT
            if (ch=='l') {
                ch=*(fmt++);
                lng=1;
            }
#endif
            switch (ch) {
                case 0:
                    goto abort;
                case 'u' : {
#ifdef  PRINTF_LONG_SUPPORT
                    if (lng)
                        uli2a(va_arg(va, unsigned long int),10,0,bf);
                    else
#endif
                    ui2a(va_arg(va, unsigned int),10,0,bf);
                    putchw(buff, &buff_offset, w,lz,bf);
                    break;
                    }
                case 'd' :  {
#ifdef  PRINTF_LONG_SUPPORT
                    if (lng)
                        li2a(va_arg(va, unsigned long int),bf);
                    else
#endif
                    i2a(va_arg(va, int),bf);
                    putchw(buff, &buff_offset, w,lz,bf);
                    break;
                    }
                case 'x': case 'X' :
#ifdef  PRINTF_LONG_SUPPORT
                    if (lng)
                        uli2a(va_arg(va, unsigned long int),16,(ch=='X'),bf);
                    else
#endif
                    ui2a(va_arg(va, unsigned int),16,(ch=='X'),bf);
                    putchw(buff, &buff_offset, w,lz,bf);
                    break;
                case 'c' :
                    buff[buff_offset++] = (char)(va_arg(va, int));
                    break;
                case 's' :
                    putchw(buff, &buff_offset, w,0,va_arg(va, char*));
                    break;
                case '%' :
                    buff[buff_offset++] = ch;
                default:
                    break;
                }
            }
        }
        va_end(va);
    abort:;
    va_end(va);
    }

