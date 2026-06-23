#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   
#include <time.h>


#define MENU_COUNT      5
#define INITIAL_STOCK   5


#define DENOM_COUNT     4
static const int DENOMINATIONS[DENOM_COUNT] = {5000, 1000, 500, 100};


#define WATER_FILL_RATE_ML_PER_SEC  100   /* 100ml / 초 */
#define COOK_MINUTE_TO_SEC           1    /* 조리 시간 1분 -> 1초 압축 */
#define PROGRESS_BAR_WIDTH          24    /* 진행률 바의 너비 */

/* ============================================================ *
 * 상태 머신 정의
 * ============================================================ */

typedef enum {
    STATE_IDLE = 0,
    STATE_MENU,
    STATE_PAYMENT,
    STATE_CUP_INSERT,
    STATE_WATER_FILL,
    STATE_COOKING,
    STATE_DONE,
    STATE_EXIT,         /* 프로그램 종료를 위한 특수 상태 */
    STATE_COUNT
} State;

/* ============================================================ *
 * 데이터 구조
 * ============================================================ */

typedef struct {
    int  id;
    char name[32];
    int  price;        /* 원 */
    int  water_ml;     /* 필요한 물량 (ml) */
    int  cook_min;     /* 조리 시간 (분) */
    int  stock;        /* 남은 재고 */
} Ramen;

typedef struct {
    State  state;          /* 현재 상태 */
    Ramen *menu;           /* 메뉴 배열 */
    int    menu_count;     /* 메뉴 개수 */
    int    selected;       /* 선택된 메뉴 인덱스 (-1: 미선택) */
    int    inserted;       /* 투입된 금액 (원) */
    int    sales_total;    /* 누적 매출 (종료 시 출력용) */
    int    sales_count;    /* 누적 판매 수량 (종료 시 출력용) */
} Kiosk;

/* ============================================================ *
 * 메뉴 데이터 (전역)
 * ============================================================ */

static Ramen g_menu[MENU_COUNT] = {
    {1, "신라면",   1500, 550, 3, INITIAL_STOCK},
    {2, "진라면",   1500, 500, 3, INITIAL_STOCK},
    {3, "너구리",   1700, 600, 5, INITIAL_STOCK},
    {4, "짜파게티", 1700, 550, 5, INITIAL_STOCK},
    {5, "안성탕면", 1500, 550, 4, INITIAL_STOCK},
};

/* ============================================================ *
 * 유틸리티 함수 (제공됨)
 * ============================================================ */

/* 화면을 지운다 */
static void clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

/* 사용자가 Enter를 누를 때까지 대기 */
static void wait_enter(const char *msg) {
    int c;
    printf("%s", msg);
    fflush(stdout);
    while ((c = getchar()) != '\n' && c != EOF) { /* drain */ }
}

/* 정수 입력을 [min, max] 범위로 받는다. 잘못된 입력은 다시 받는다. */
static int read_int(const char *prompt, int min, int max) {
    int value;
    char buf[64];
    while (1) {
        printf("%s", prompt);
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) {
            clearerr(stdin);
            continue;
        }
        if (sscanf(buf, "%d", &value) == 1 && value >= min && value <= max) {
            return value;
        }
        printf("  [!] 잘못된 입력입니다. %d ~ %d 범위의 숫자를 입력하세요.\n", min, max);
    }
}

/* 화면 상단 헤더 출력 */
static void print_header(const char *title) {
    printf("==============================================\n");
    printf("  %s\n", title);
    printf("==============================================\n");
}

/* ============================================================ *
 * 보조 함수 (구현 완료)
 * ============================================================ */

/* 거스름돈을 단위별 개수로 계산 (탐욕 알고리즘 기법 적용) */
static void calculate_change(int amount, int counts[DENOM_COUNT]) {
    for (int i = 0; i < DENOM_COUNT; i++) {
        counts[i] = amount / DENOMINATIONS[i];
        amount %= DENOMINATIONS[i];
    }
}

/* 진행률 막대를 한 줄에 실시간 출력 (\r 캐리지 리턴 활용) */
static void print_progress(const char *label, double current, double total) {
    if (current > total) current = total;
    
    double percentage = (current / total) * 100.0;
    int filled_width = (int)((current / total) * PROGRESS_BAR_WIDTH);
    
    if (filled_width > PROGRESS_BAR_WIDTH) filled_width = PROGRESS_BAR_WIDTH;
    if (filled_width < 0) filled_width = 0;

    printf("\r%s [", label);
    for (int i = 0; i < filled_width; i++) {
        printf("#");
    }
    for (int i = filled_width; i < PROGRESS_BAR_WIDTH; i++) {
        printf("-");
    }
    printf("] %3.0f%% (%.1f/%.1f초)", percentage, current, total);
    fflush(stdout);
}

/* ============================================================ *
 * 상태 핸들러 함수 (구현 완료)
 * ============================================================ */

/* IDLE: 대기 화면 */
static State handle_idle(Kiosk *k) {
    clear_screen();
    
    /* 전체 품절 여부 검사 예외 처리 */
    int total_stock = 0;
    for (int i = 0; i < k->menu_count; i++) {
        total_stock += k->menu[i].stock;
    }
    if (total_stock == 0) {
        print_header("전체 품절 안내");
        printf("  현재 모든 라면 제품이 품절되었습니다.\n");
        printf("  시스템을 종료합니다. 이용에 불편을 드려 죄송합니다.\n");
        wait_enter("  Enter를 누르면 종료됩니다... ");
        return STATE_EXIT;
    }

    print_header("즉석라면 자판기");
    printf("  즉석라면 자판기에 오신 것을 환영합니다.\n");
    printf("  라면을 빠르고 간편하게!\n\n");
    printf("  시작하려면 아무 키나 입력하세요. (종료: q + Enter)\n\n");
    printf("  > ");
    fflush(stdout);

    char buf[64];
    if (fgets(buf, sizeof(buf), stdin)) {
        if (buf[0] == 'q' || buf[0] == 'Q') {
            return STATE_EXIT;
        }
    }
    return STATE_MENU;
}

/* MENU: 메뉴 선택 */
static State handle_menu(Kiosk *k) {
    clear_screen();
    print_header("메뉴 선택");
    
    for (int i = 0; i < k->menu_count; i++) {
        if (k->menu[i].stock > 0) {
            printf("  %d. %-10s - %d원  (재고 %d개)\n", 
                   k->menu[i].id, k->menu[i].name, k->menu[i].price, k->menu[i].stock);
        } else {
            printf("  %d. %-10s - %d원  [품절]\n", 
                   k->menu[i].id, k->menu[i].name, k->menu[i].price);
        }
    }
    printf("  0. 취소 (대기 화면으로 복귀)\n\n");
    
    int choice = read_int("  메뉴 번호를 선택하세요: ", 0, k->menu_count);
    if (choice == 0) {
        return STATE_IDLE;
    }
    
    int index = choice - 1;
    /* 선택한 라면의 품절 예외 흐름 제어 */
    if (k->menu[index].stock <= 0) {
        printf("  [!] 죄송합니다. 선택하신 라면은 품절되었습니다.\n");
        wait_enter("  Enter를 누르면 메뉴 화면으로 돌아갑니다...");
        return STATE_MENU;
    }
    
    k->selected = index;
    k->inserted = 0;
    return STATE_PAYMENT;
}

/* PAYMENT: 결제 처리 */
static State handle_payment(Kiosk *k) {
    Ramen *selected_ramen = &k->menu[k->selected];
    
    while (k->inserted < selected_ramen->price) {
        clear_screen();
        print_header("결제 진행");
        printf("  선택된 메뉴: %s\n", selected_ramen->name);
        printf("  가격: %d원\n", selected_ramen->price);
        printf("  현재 투입 금액: %d원\n", k->inserted);
        
        int remaining = selected_ramen->price - k->inserted;
        printf("  남은 결제 금액: %d원\n\n", remaining);
        
        printf("  금액을 투입하십시오:\n");
        printf("  1. 100원\n");
        printf("  2. 500원\n");
        printf("  3. 1000원\n");
        printf("  4. 5000원\n");
        printf("  0. 결제 취소 (환불)\n\n");
        
        int choice = read_int("  선택: ", 0, 4);
        
        /* 결제 도중 전면 취소 및 투입금 전액 환불 흐름 처리 */
        if (choice == 0) {
            clear_screen();
            print_header("결제 취소");
            if (k->inserted > 0) {
                int refund_counts[DENOM_COUNT] = {0};
                calculate_change(k->inserted, refund_counts);
                printf("  결제가 취소되었습니다. 투입된 금액 %d원을 환불합니다.\n", k->inserted);
                printf("  반환 화폐 내역:\n");
                for (int i = 0; i < DENOM_COUNT; i++) {
                    if (refund_counts[i] > 0) {
                        printf("    %5d원 x %d\n", DENOMINATIONS[i], refund_counts[i]);
                    }
                }
            } else {
                printf("  투입된 금액이 없어 즉시 취소 처리되었습니다.\n");
            }
            wait_enter("\n  Enter를 누르면 대기 화면으로 복귀합니다... ");
            k->selected = -1;
            k->inserted = 0;
            return STATE_IDLE;
        }
        
        int money = 0;
        if (choice == 1) money = 100;
        else if (choice == 2) money = 500;
        else if (choice == 3) money = 1000;
        else if (choice == 4) money = 5000;
        
        k->inserted += money;
    }
    
    /* 누적 투입 금액이 제품 가격 이상을 달성했을 때의 처리 */
    clear_screen();
    print_header("결제 완료");
    printf("  선택된 메뉴: %s\n", selected_ramen->name);
    printf("  가격: %d원\n", selected_ramen->price);
    printf("  현재 투입 금액: %d원\n", k->inserted);
    printf("  남은 결제 금액: 0원\n\n");
    printf("  결제가 성공적으로 완료되었습니다!\n");
    
    int change = k->inserted - selected_ramen->price;
    if (change > 0) {
        int change_counts[DENOM_COUNT] = {0};
        calculate_change(change, change_counts);
        printf("  거스름돈 반환 내역 (%d원):\n", change);
        for (int i = 0; i < DENOM_COUNT; i++) {
            if (change_counts[i] > 0) {
                printf("    %5d원 x %d\n", DENOMINATIONS[i], change_counts[i]);
            }
        }
    } else {
        printf("  거스름돈이 발생하지 않았습니다.\n");
    }
    
    wait_enter("\n  Enter를 눌러 다음 단계로... ");
    return STATE_CUP_INSERT;
}

/* CUP_INSERT: 전용 용기 투입 대기 */
static State handle_cup_insert(Kiosk *k) {
    (void)k;
    clear_screen();
    print_header("컵 투입");
    printf("  자판기 하단 조리대에 전용 컵을 정위치에 넣어주세요.\n");
    printf("  (※ 본 프로그램은 시뮬레이션이므로 아래 프롬프트에서 Enter를 입력하세요)\n\n");
    wait_enter("  컵을 넣으셨으면 Enter... ");
    return STATE_WATER_FILL;
}

/* WATER_FILL: 물 주입 시뮬레이션 */
static State handle_water_fill(Kiosk *k) {
    clear_screen();
    print_header("물 주입 중");
    Ramen *selected_ramen = &k->menu[k->selected];
    printf("  %s에 필요한 온수 %dml를 주입하고 있습니다.\n\n", selected_ramen->name, selected_ramen->water_ml);
    
    double total_time = (double)selected_ramen->water_ml / WATER_FILL_RATE_ML_PER_SEC;
    double elapsed = 0.0;
    
    /* 100ms 간격 실시간 갱신 인디케이터 구동 루프 */
    while (elapsed < total_time) {
        print_progress("  [물주입]", elapsed, total_time);
        usleep(100000); /* 100,000 microseconds = 100ms */
        elapsed += 0.1;
    }
    print_progress("  [물주입]", total_time, total_time);
    printf("\n\n  온수 주입이 안전하게 완료되었습니다!\n");
    
    usleep(500000); /* 부드러운 화면 전환을 위한 비주얼 딜레이 */
    return STATE_COOKING;
}

/* COOKING: 고온 라면 조리 시뮬레이션 */
static State handle_cooking(Kiosk *k) {
    clear_screen();
    print_header("조리 중");
    Ramen *selected_ramen = &k->menu[k->selected];
    printf("  %s를 맛있게 조리하고 있습니다 (%d분 예정).\n\n", selected_ramen->name, selected_ramen->cook_min);
    
    double total_time = (double)selected_ramen->cook_min * COOK_MINUTE_TO_SEC;
    double elapsed = 0.0;
    
    /* 100ms 간격 실시간 갱신 인디케이터 구동 루프 */
    while (elapsed < total_time) {
        print_progress("  [조리중]", elapsed, total_time);
        usleep(100000);
        elapsed += 0.1;
    }
    print_progress("  [조리중]", total_time, total_time);
    printf("\n\n  라면 조리가 완전히 끝났습니다!\n");
    
    /* 최종 내부 재고 자원 차감 및 누적 정산 통계 업데이트 */
    selected_ramen->stock--;
    k->sales_total += selected_ramen->price;
    k->sales_count++;
    
    usleep(500000);
    return STATE_DONE;
}

/* DONE: 라면 제공 및 시스템 변수 초기화 */
static State handle_done(Kiosk *k) {
    clear_screen();
    print_header("조리 완료");
    Ramen *selected_ramen = &k->menu[k->selected];
    
    printf("  주문하신 조리가 무사히 완성되었습니다!\n\n");
    printf("  >> %s <<\n", selected_ramen->name);
    printf("==============================================\n");
    printf("  [주의] 용기가 매우 뜨거우니 화상에 조심하세요!\n\n");
    
    wait_enter("  라면을 꺼내어 가져가셨으면 Enter를 누르세요... ");
    
    /* 다음 주문 세션을 위해 활성 선택 플래그 및 임시 캐시 데이터 초기화 */
    k->selected = -1;
    k->inserted = 0;
    return STATE_IDLE;
}

/* ============================================================ *
 * 메인 루프 (템플릿 기준 유지)
 * ============================================================ */

typedef State (*StateHandler)(Kiosk *);

int main(void) {
    Kiosk kiosk = {
        .state = STATE_IDLE,
        .menu = g_menu,
        .menu_count = MENU_COUNT,
        .selected = -1,
        .inserted = 0,
        .sales_total = 0,
        .sales_count = 0,
    };

    /*
     * 함수 포인터 배열로 상태 → 핸들러 매핑.
     * designated initializer를 사용해 인덱스를 명시한다.
     */
    StateHandler handlers[STATE_COUNT] = {
        [STATE_IDLE]        = handle_idle,
        [STATE_MENU]        = handle_menu,
        [STATE_PAYMENT]     = handle_payment,
        [STATE_CUP_INSERT]  = handle_cup_insert,
        [STATE_WATER_FILL]  = handle_water_fill,
        [STATE_COOKING]     = handle_cooking,
        [STATE_DONE]        = handle_done,
        [STATE_EXIT]        = NULL,
    };

    /* 메인 루프: 함수 포인터로 현재 상태의 핸들러 호출 */
    while (kiosk.state != STATE_EXIT) {
        StateHandler h = handlers[kiosk.state];
        if (h == NULL) {
            fprintf(stderr, "FATAL: no handler for state %d\n", kiosk.state);
            return 1;
        }
        kiosk.state = h(&kiosk);
    }

    /* 종료 메시지 및 총 매출 정산 보고 통계 출력 */
    clear_screen();
    print_header("이용해 주셔서 감사합니다");
    printf("  누적 매출: %d원\n", kiosk.sales_total);
    printf("  판매 수량: %d개\n", kiosk.sales_count);
    printf("==============================================\n");

    return 0;
}