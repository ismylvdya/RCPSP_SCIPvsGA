#include "scip/scip.h"              // Основной интерфейс SCIP
#include "scip/scipdefplugins.h"    // Стандартные плагины SCIP (LP, branching, heuristics)
#include "rcpsp_parser.h"

#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <map>
#include <utility>

// Qt визуализация диаграмма Ганта
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QBrush>
#include <QColor>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QPainter>
#include <QScrollArea>
#include <QFrame>
#include <QGraphicsPathItem>
#include <QPainterPath>

namespace fs = std::filesystem;

/* ===================================================================
   Виджет таймлайна (ось времени, фиксированная внизу)
   =================================================================== */
class TimelineWidget : public QWidget {
public:
    TimelineWidget(double maxTime, double scale, int margin, int totalWidth,
                   QWidget* parent = nullptr)
        : QWidget(parent),
          maxTime(maxTime),
          scale(scale),
          margin(margin),
          totalWidth(totalWidth)
    {
        setFixedHeight(50);
        setMinimumWidth(totalWidth);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        int tickHeight = 10;
        int timeStep   = 1;
        int timelineY  = 10;

        for (int t = 0; t <= (int)maxTime; t += timeStep) {
            int x = margin + (int)(t * scale);

            painter.drawLine(x, timelineY, x, timelineY + tickHeight);

            QString label = QString::number(t);
            QRect rect = painter.fontMetrics().boundingRect(label);
            painter.drawText(x - rect.width() / 2,
                             timelineY + tickHeight + 2 + rect.height(),
                             label);
        }
    }

private:
    double maxTime;
    double scale;
    int margin;
    int totalWidth;
};

/* ===================================================================
   Отрисовка диаграммы Ганта
   =================================================================== */
void showGanttChart(const std::vector<std::pair<int, double>>& starts,
                    const std::vector<Task>& tasks)
{
    int taskHeight = 25;
    int spacing    = 5;
    int margin     = 20;
    double scale   = 20.0; // пикселей на единицу времени

    /* --- Быстрый доступ к задачам по id --- */
    std::map<int, const Task*> task_map;
    for (const auto& task : tasks)
        task_map[task.id] = &task;

    /* --- Геометрия сцены --- */
    int height = margin * 2 + (taskHeight + spacing) * starts.size();
    double maxTime = 0.0;

    for (const auto& [id, start] : starts)
        maxTime = std::max(maxTime, start + task_map[id]->duration);

    int width = margin * 2 + (int)(scale * maxTime);

    QGraphicsScene* scene = new QGraphicsScene(0, 0, width, height);

    /* --- Отрисовка задач --- */
    for (size_t i = 0; i < starts.size(); ++i) {
        int task_id      = starts[i].first;
        double startTime = starts[i].second;
        const Task* task = task_map[task_id];
        if (!task) continue;

        int x = margin + (int)(startTime * scale);
        int y = margin + i * (taskHeight + spacing);
        int w = (int)(task->duration * scale);

        /* --- Пунктирные проекции --- */
        // (не для фиктивных задач):
        if (task->duration > 0 &&
            task_id != 1 &&
            task_id != (int)tasks.size())
        {
            int timelineHeight = 50;
            int timelineY = height - timelineHeight;

            QPen dashedPen(QColor(70, 70, 70));
            dashedPen.setWidthF(1.0);
            dashedPen.setDashPattern({5, 5});

            scene->addLine(x,     y + taskHeight, x,     timelineY, dashedPen);
            scene->addLine(x + w, y + taskHeight, x + w, timelineY, dashedPen);
        }


        /* --- Прямоугольник и подпись (не для фиктивных задач) --- */
        if (task->duration > 0) {

            int radius = taskHeight / 4;
            QPainterPath path;
            path.addRoundedRect(x, y, w, taskHeight, radius, radius);

            QGraphicsPathItem* rect = scene->addPath(
                path,
                QPen(Qt::black),
                QBrush(QColor("#61C554"))
            );

            rect->setToolTip(QString("Task %1\nstart = %2\nduration = %3")
                             .arg(task_id)
                             .arg(startTime)
                             .arg(task->duration));

            /* --- Подпись задачи --- */
            QGraphicsTextItem* text = scene->addText(QString::number(task_id));
            text->setDefaultTextColor(Qt::black);  // ← ты уже хотел чёрный
            text->setPos(
                x + w / 2 - text->boundingRect().width() / 2,
                y + taskHeight / 2 - text->boundingRect().height() / 2
            );
        }

    }

    /* --- Компоновка окна --- */
    QWidget* mainWidget = new QWidget();
    mainWidget->setWindowTitle("Gantt Chart");

    QVBoxLayout* layout = new QVBoxLayout(mainWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QGraphicsView* view = new QGraphicsView(scene);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QScrollArea* timelineArea = new QScrollArea();
    timelineArea->setFixedHeight(50);
    timelineArea->setFrameShape(QFrame::NoFrame);
    timelineArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    timelineArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    TimelineWidget* timeline =
        new TimelineWidget(maxTime, scale, margin, width);
    timelineArea->setWidget(timeline);

    QObject::connect(view->horizontalScrollBar(), &QScrollBar::valueChanged,
                     [timelineArea](int v) {
                         timelineArea->horizontalScrollBar()->setValue(v);
                     });

    layout->addWidget(view, 1);
    layout->addWidget(timelineArea, 0);

    mainWidget->resize((width + 50) / 2, (height + 50) / 2 + 50);
    mainWidget->show();
}



int main()
{
    /* ---------- Выбор входного SM-файла ---------- */
    const std::string dir_path = "../sm_files/j30.sm";
    std::string sm_file;

    try {
        for (const auto& entry : fs::directory_iterator(dir_path)) {
            if (entry.is_regular_file() &&
                entry.path().filename() != ".DS_Store") {
                sm_file = entry.path().string();
                break;
            }
        }

        if (sm_file.empty()) {
            std::cerr << "В директории нет SM-файлов\n";
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Ошибка доступа к директории: " << e.what() << "\n";
        return 1;
    }

    /* ---------- Парсинг RCPSP ---------- */
    RCPSPInstance inst;
    try {
        inst = parse_sm_file(sm_file);
    } catch (...) {
        std::cerr << "Ошибка чтения SM-файла\n";
        return 1;
    }

    /* ---------- Инициализация SCIP ---------- */
    SCIP* scip = nullptr;
    SCIP_CALL(SCIPcreate(&scip));
    SCIP_CALL(SCIPincludeDefaultPlugins(scip));
    SCIP_CALL(SCIPcreateProbBasic(scip, "rcpsp_30"));

    /* ---------- Переменные начала задач ---------- */
    std::vector<SCIP_VAR*> start_vars(inst.n_jobs, nullptr);

    for (const auto& t : inst.tasks) {
        SCIP_VAR* var = nullptr;
        std::string name = "t" + std::to_string(t.id);
        SCIP_CALL(SCIPcreateVarBasic(
            scip, &var, name.c_str(),
            0.0, SCIPinfinity(scip), 1e-4, SCIP_VARTYPE_INTEGER));
        SCIP_CALL(SCIPaddVar(scip, var));
        start_vars[t.id - 1] = var;
    }

    /* ---------- Переменная makespan ---------- */
    SCIP_VAR* makespan = nullptr;
    SCIP_CALL(SCIPcreateVarBasic(
        scip, &makespan, "makespan",
        0.0, SCIPinfinity(scip), 1.0, SCIP_VARTYPE_CONTINUOUS));
    SCIP_CALL(SCIPaddVar(scip, makespan));

    /* ---------- Ограничения предшествования ---------- */
    for (const auto& t : inst.tasks) {
        for (int succ : t.successors) {
            SCIP_CONS* cons = nullptr;
            SCIP_VAR* vars[]  = { start_vars[succ - 1], start_vars[t.id - 1] };
            SCIP_Real coefs[] = { 1.0, -1.0 };

            SCIP_CALL(SCIPcreateConsBasicLinear(
                scip, &cons, "prec",
                2, vars, coefs,
                t.duration, SCIPinfinity(scip)));

            SCIP_CALL(SCIPaddCons(scip, cons));
            SCIP_CALL(SCIPreleaseCons(scip, &cons));
        }
    }

    /* ---------- Ограничения makespan ---------- */
    // ( привязываем makespan к концу последней задачи: для каждой задачи t должен быть больше чем конец данной t )
    for (const auto& t : inst.tasks) {
        SCIP_CONS* cons = nullptr;
        SCIP_VAR* vars[]  = { makespan, start_vars[t.id - 1] };
        SCIP_Real coefs[] = { 1.0, -1.0 };

        SCIP_CALL(SCIPcreateConsBasicLinear(
            scip, &cons, "makespan",
            2, vars, coefs,
            t.duration, SCIPinfinity(scip)));

        SCIP_CALL(SCIPaddCons(scip, cons));
        SCIP_CALL(SCIPreleaseCons(scip, &cons));
    }



    /* =======================================================================
        Ограничения ресурсов (разные виды 1. 2. 3.)
        ======================================================================= */

    /* -----------------------------------------------------------------------
        1. Ограничения ёмкости ресурсов (resource capacity constraints)
        Сравниваем каждую задачу с каждой. Если конфликтуют по ресурсу, то вводится дизъюнкция:
        либо первая задача выполняется раньше второй, либо наоборот
        ( через бинарную переменную и big-M ограничения )
        ----------------------------------------------------------------------- */
    for (int r = 0; r < inst.n_resources; ++r) {                // по ресурсам
        int capacity = inst.resources[r].capacity;

        for (size_t i = 0; i < inst.tasks.size(); ++i) {        // по таскам №1
            const auto& task_i = inst.tasks[i];
            int usage_i = (r < (int)task_i.resources.size()) ? task_i.resources[r] : 0;
            if (usage_i == 0) continue;

            for (size_t j = i + 1; j < inst.tasks.size(); ++j) {        // по таскам №2
                const auto& task_j = inst.tasks[j];
                int usage_j = (r < (int)task_j.resources.size()) ? task_j.resources[r] : 0;
                if (usage_j == 0) continue;

                /* Если суммарное потребление превышает ёмкость ресурса,
                   задачи не могут выполняться одновременно */
                if (usage_i + usage_j > capacity) {

                    // Бинарная переменная y_i_j_r порядка выполнения задач  --  тоже оптимизируемая переменная в SCIP
                    std::string y_name = "y_" + std::to_string(task_i.id) + "_" +
                                         std::to_string(task_j.id) + "_r" +
                                         std::to_string(r);
                    SCIP_VAR* y_var = nullptr;
                    SCIP_CALL(SCIPcreateVarBasic(
                        scip, &y_var, y_name.c_str(),
                        0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY));
                    SCIP_CALL(SCIPaddVar(scip, y_var));

                    // Большая константа для big-M ограничений
                    SCIP_Real M = 0.0;
                    for (const auto& t : inst.tasks)
                        M += t.duration;
                    M += 1000;

                    /* y = 1 ⇒ task_i завершается до начала task_j */
                    SCIP_CONS* cons1 = nullptr;
                    SCIP_VAR* vars1[] = {
                        start_vars[task_j.id - 1],
                        start_vars[task_i.id - 1],
                        y_var
                    };
                    SCIP_Real coefs1[] = {1.0, -1.0, -M};


                    SCIP_CALL(SCIPcreateConsBasicLinear(
                        scip, &cons1,
                        ("resource_order_" + std::to_string(task_i.id) + "_" +
                         std::to_string(task_j.id) + "_r" +
                         std::to_string(r) + "_1").c_str(),
                        3, vars1, coefs1,
                        task_i.duration - M, SCIPinfinity(scip)));
                    SCIP_CALL(SCIPaddCons(scip, cons1));
                    SCIP_CALL(SCIPreleaseCons(scip, &cons1));

                    /* y = 0 ⇒ task_j завершается до начала task_i */
                    SCIP_CONS* cons2 = nullptr;
                    SCIP_VAR* vars2[] = {
                        start_vars[task_i.id - 1],
                        start_vars[task_j.id - 1],
                        y_var
                    };
                    SCIP_Real coefs2[] = {1.0, -1.0, M};

                    SCIP_CALL(SCIPcreateConsBasicLinear(
                        scip, &cons2,
                        ("resource_order_" + std::to_string(task_i.id) + "_" +
                         std::to_string(task_j.id) + "_r" +
                         std::to_string(r) + "_2").c_str(),
                        3, vars2, coefs2,
                        task_j.duration, SCIPinfinity(scip)));
                    SCIP_CALL(SCIPaddCons(scip, cons2));
                    SCIP_CALL(SCIPreleaseCons(scip, &cons2));

                    SCIP_CALL(SCIPreleaseVar(scip, &y_var));
                }
            }
        }
    }



    /* -----------------------------------------------------------------------
        2. Ограничения недоступных интервалов ресурсов
        Каждая задача либо полностью завершается до начала интервала,
        либо начинается после его окончания
        ----------------------------------------------------------------------- */

    // resource_unavailability[r] — список временных интервалов [L, U), в которые ресурс r полностью недоступен
    std::map<int, std::vector<std::pair<int,int>>> resource_unavailability;

    resource_unavailability[0] = {{30, 40}};
    resource_unavailability[1] = {{30, 40}};
    resource_unavailability[2] = {{30, 40}};
    resource_unavailability[3] = {{30, 40}};

    for (const auto& task : inst.tasks) {
        for (int r = 0; r < inst.n_resources; ++r) {
            int usage = (r < (int)task.resources.size()) ? task.resources[r] : 0;
            if (usage == 0) continue;

            if (resource_unavailability.find(r) != resource_unavailability.end()) { // если для ресурса r заданы ограничения в resource_unavailability
                for (const auto& [L, U] : resource_unavailability[r]) {

                    // Бинарная переменная выбора стороны интервала
                    std::string z_name = "z_" + std::to_string(task.id) +
                                         "_r" + std::to_string(r) +
                                         "_" + std::to_string(L) +
                                         "_" + std::to_string(U);
                    SCIP_VAR* z_var = nullptr;
                    SCIP_CALL(SCIPcreateVarBasic(
                        scip, &z_var, z_name.c_str(),
                        0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY));
                    SCIP_CALL(SCIPaddVar(scip, z_var));

                    SCIP_Real M = 0.0;
                    for (const auto& t : inst.tasks)
                        M += t.duration;
                    M += 1000;

                    /* z = 1 ⇒ задача завершается до L */
                    SCIP_CONS* cons_before = nullptr;
                    SCIP_VAR* vars_before[] = {
                        start_vars[task.id - 1],
                        z_var
                    };
                    SCIP_Real coefs_before[] = {1.0, M};

                    SCIP_CALL(SCIPcreateConsBasicLinear(
                        scip, &cons_before,
                        ("unavail_before_" + std::to_string(task.id) +
                         "_r" + std::to_string(r) + "_" +
                         std::to_string(L)).c_str(),
                        2, vars_before, coefs_before,
                        -SCIPinfinity(scip),
                        L - task.duration + M));
                    SCIP_CALL(SCIPaddCons(scip, cons_before));
                    SCIP_CALL(SCIPreleaseCons(scip, &cons_before));

                    /* z = 0 ⇒ задача начинается после U */
                    SCIP_CONS* cons_after = nullptr;
                    SCIP_VAR* vars_after[] = {
                        start_vars[task.id - 1],
                        z_var
                    };
                    SCIP_Real coefs_after[] = {1.0, M};

                    SCIP_CALL(SCIPcreateConsBasicLinear(
                        scip, &cons_after,
                        ("unavail_after_" + std::to_string(task.id) +
                         "_r" + std::to_string(r) + "_" +
                         std::to_string(U)).c_str(),
                        2, vars_after, coefs_after,
                        U, SCIPinfinity(scip)));
                    SCIP_CALL(SCIPaddCons(scip, cons_after));
                    SCIP_CALL(SCIPreleaseCons(scip, &cons_after));

                    SCIP_CALL(SCIPreleaseVar(scip, &z_var));
                }
            }
        }
    }



    /* ------------------------------------------------------------
        3. Time-dependent capacity ресурсов
        capacity[r][t] — ёмкость ресурса r в момент времени t
        ------------------------------------------------------------ */
    // число переменных  =  кол-во тасков  x  кол-во моментов времени

    std::map<int, std::map<int, int>> time_capacity;

    // пример: по 3 момента для каждого из 4 ресурсов
    // ресурс 0
    time_capacity[0][10] = 2;
    time_capacity[0][11] = 1;
    time_capacity[0][12] = 3;

    // ресурс 1
    time_capacity[1][10] = 1;
    time_capacity[1][11] = 1;
    time_capacity[1][12] = 2;

    // ресурс 2
    time_capacity[2][10] = 2;
    time_capacity[2][11] = 2;
    time_capacity[2][12] = 2;

    // ресурс 3
    time_capacity[3][10] = 1;
    time_capacity[3][11] = 2;
    time_capacity[3][12] = 1;

    // Переменные x_{i,t} = 1  <=>  задача i активна в момент t

    std::map<std::pair<int,int>, SCIP_VAR*> x_vars;

    // Большое M
    SCIP_Real M = 0.0;
    for (const auto& t : inst.tasks)
        M += t.duration;
    M += 1000;

    // определение индикаторов x_task_t
    for (const auto& task : inst.tasks) {
        if (task.duration == 0) continue;

        for (const auto& [r, cap_map] : time_capacity) {        // по всем ресурсам r
            for (const auto& [t, cap] : cap_map) {      // по всем временам t

                std::string name = "x_" + std::to_string(task.id) +
                                   "_t" + std::to_string(t);

                SCIP_VAR* x = nullptr;            // x = 1   =>   задача task выполняется в t   ;   x = 0   =>   не выполняется   (оптимизируется SCIP-ом)
                SCIP_CALL(SCIPcreateVarBasic(
                    scip, &x, name.c_str(),
                    0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY));
                SCIP_CALL(SCIPaddVar(scip, x));

                x_vars[{task.id, t}] = x;

                /* start_i <= t + M*(1-x) */        // задача началась до текущего t
                SCIP_CONS* c1 = nullptr;
                SCIP_VAR* v1[] = { start_vars[task.id - 1], x };
                SCIP_Real a1[] = { 1.0,  M };

                SCIP_CALL(SCIPcreateConsBasicLinear(
                    scip, &c1, "active_lb",
                    2, v1, a1,
                    -SCIPinfinity(scip),
                    t + M));
                SCIP_CALL(SCIPaddCons(scip, c1));
                SCIP_CALL(SCIPreleaseCons(scip, &c1));

                /* start_i + dur_i >= t+1 - M*(1-x) */    // задача закончится после текущего t
                SCIP_CONS* c2 = nullptr;
                SCIP_VAR* v2[] = { start_vars[task.id - 1], x };
                SCIP_Real a2[] = { 1.0, -M };

                SCIP_CALL(SCIPcreateConsBasicLinear(
                    scip, &c2, "active_ub",
                    2, v2, a2,
                    t + 1 - task.duration - M,
                    SCIPinfinity(scip)));
                SCIP_CALL(SCIPaddCons(scip, c2));
                SCIP_CALL(SCIPreleaseCons(scip, &c2));
            }
        }
    }

    // передача ограничений на ресурсы во времени в SCIP

    for (const auto& [r, cap_map] : time_capacity) {
        for (const auto& [t, cap] : cap_map) {

            SCIP_CONS* cons = nullptr;
            std::vector<SCIP_VAR*> vars;
            std::vector<SCIP_Real> coefs;

            for (const auto& task : inst.tasks) {
                int usage = (r < (int)task.resources.size())  // теоретически задача может использовать ресурс r
                            ? task.resources[r]
                            : 0;
                if (usage == 0 || task.duration == 0) continue;

                auto it = x_vars.find({task.id, t});     // переменная x_i_t
                if (it == x_vars.end()) continue;

                vars.push_back(it->second);
                coefs.push_back((SCIP_Real)usage);
                // sum ( usage_i_r x x_i_t )   -- то есть сколько данного ресурса r используется в t
            }

            if (!vars.empty()) {
                SCIP_CALL(SCIPcreateConsBasicLinear(
                    scip, &cons,
                    ("cap_r" + std::to_string(r) +
                     "_t" + std::to_string(t)).c_str(),
                    vars.size(),
                    vars.data(),
                    coefs.data(),
                    -SCIPinfinity(scip),
                    cap));
                    // то есть  -inf  <  sum ( usage_i_r x x_i_t )  <  капасити r
                SCIP_CALL(SCIPaddCons(scip, cons));
                SCIP_CALL(SCIPreleaseCons(scip, &cons));
            }
        }
    }



    /* ---------- Решение ---------- */
    auto t_start = std::chrono::high_resolution_clock::now();
    SCIP_CALL(SCIPsolve(scip));
    auto t_end = std::chrono::high_resolution_clock::now();

    SCIP_SOL* sol = SCIPgetBestSol(scip);
    if (!sol) {
        std::cout << "No feasible solution\n";
        return 0;
    }

    std::vector<std::pair<int, double>> starts;
    for (int id = 1; id <= inst.n_jobs; ++id)
        starts.emplace_back(id,
            SCIPgetSolVal(scip, sol, start_vars[id - 1]));

    /* ---------- Визуализация ---------- */
    int argc = 0;
    char* argv[] = {nullptr};
    QApplication app(argc, argv);
    showGanttChart(starts, inst.tasks);

    return app.exec();
}
