#include "scip/scip.h"              // верхнеуровневая обертка (чисто вызывает остальные скиповские хедеры)
#include "scip/scipdefplugins.h"    // вызывает все методы которые в текущем main используются (ветвления, отсечения, эвристики и тд)
#include "rcpsp_parser.h"
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <map>
#include <utility>

#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QBrush>
#include <QColor>

namespace fs = std::filesystem;


void showGanttChart(const std::vector<std::pair<int, double>>& starts,
                    const std::vector<Task>& tasks)
{
    int taskHeight = 30;
    int spacing = 10;
    int margin = 20;

    double scale = 20.0; // пикселей на единицу времени

    int height = margin * 2 + (taskHeight + spacing) * tasks.size();
    double maxTime = 0;
    for (size_t i = 0; i < tasks.size(); ++i)
        maxTime = std::max(maxTime, starts[i].second + tasks[i].duration);
    int width = margin * 2 + (int)(scale * maxTime);

    // Добавляем место снизу для таймлайна
    int timelineHeight = 50;
    height += timelineHeight;

    QGraphicsScene* scene = new QGraphicsScene(0, 0, width, height);

    // Рисуем задачи
    for (size_t i = 0; i < tasks.size(); ++i) {
        int y = margin + i * (taskHeight + spacing);
        int x = margin + (int)(starts[i].second * scale);
        int w = (int)(tasks[i].duration * scale);

        // QGraphicsRectItem* rect = scene->addRect(x, y, w, taskHeight, QPen(Qt::black), QBrush(Qt::blue));
        QGraphicsRectItem* rect = scene->addRect(
            x, y, w, taskHeight,
            QPen(Qt::black),
            QBrush(QColor("#61C554"))  // hex-код цвета
        );


        rect->setToolTip(QString("Task %1: start=%2, duration=%3")
                         .arg(tasks[i].id)
                         .arg(starts[i].second)
                         .arg(tasks[i].duration));

        // Номер задачи по центру
        QGraphicsTextItem* text = scene->addText(QString::number(tasks[i].id));
        text->setDefaultTextColor(Qt::white);
        QRectF rectBounds = rect->rect();
        text->setPos(x + rectBounds.width()/2 - text->boundingRect().width()/2,
                     y + rectBounds.height()/2 - text->boundingRect().height()/2);
    }

    // Рисуем таймлайн
    int timelineY = margin + (taskHeight + spacing) * tasks.size() + 10; // чуть ниже задач
    int tickHeight = 10;
    int timeStep = 1; // шаг времени на таймлайне, можно увеличить

    for (int t = 0; t <= (int)maxTime; t += timeStep) {
        int x = margin + (int)(t * scale);
        // Вертикальная линия
        scene->addLine(x, timelineY, x, timelineY + tickHeight, QPen(Qt::black));
        // Метка времени
        QGraphicsTextItem* timeLabel = scene->addText(QString::number(t));
        timeLabel->setPos(x - timeLabel->boundingRect().width()/2, timelineY + tickHeight + 2);
    }

    QGraphicsView* view = new QGraphicsView(scene);
    view->setWindowTitle("Gantt Chart");

    // исходный размер
    int originalWidth = width + 50;
    int originalHeight = height + 50;

    // делаем окно в два раза меньше
    view->resize(originalWidth / 2, originalHeight / 2);

    view->show();

}







int main() {
    // const std::string sm_file = "/Users/chumbulev/CLionProjects/proba/sm_files/j30.sm/j3010_1.sm";

    const std::string dir_path = "../sm_files/j30.sm";

    std::string sm_file;
    try {
        for (const auto& entry : fs::directory_iterator(dir_path)) {
            if (!entry.is_regular_file())
                continue;

            if (entry.path().filename() == ".DS_Store")
                continue;  // пропускаем .DS_Store

            sm_file = entry.path().string();
            break;
        }


        if (sm_file.empty()) {
            std::cerr << "Ошибка: в директории нет файлов." << std::endl;
            return 1;
        }

        std::cout << "Выбран файл: " << sm_file << std::endl;
        // дальше используем sm_file
    } catch (const std::exception& e) {
        std::cerr << "Ошибка доступа к директории: " << e.what() << std::endl;
        return 1;
    }

    // Парсим задачу
    RCPSPInstance inst;
    try {
        inst = parse_sm_file(sm_file);
    } catch (const std::runtime_error& e) {
        std::cerr << "Error opening SM file: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Parsed " << inst.n_jobs << " tasks, " << inst.n_resources << " resources.\n";

    // Создаем SCIP
    SCIP* scip = nullptr;
    SCIP_CALL(SCIPcreate(&scip));
    SCIP_CALL(SCIPincludeDefaultPlugins(scip)); // тут LP-солвер, ветвления-отсечения, эвристики
    SCIP_CALL(SCIPcreateProbBasic(scip, "rcpsp_30"));



    std::vector<SCIP_VAR*> start_vars(inst.n_jobs, nullptr); // каждая переменная -- момент начала задачи

    // Создаем переменные начала задач по id, чтобы start_vars[id-1] соответствовал t{id}
    for (const auto& t : inst.tasks) {
        std::string name = "t" + std::to_string(t.id); // строчное имя переменной
        SCIP_VAR* var = nullptr;
        SCIP_CALL(SCIPcreateVarBasic(scip, &var, name.c_str(), 0.0, SCIPinfinity(scip), 0.0, SCIP_VARTYPE_INTEGER)); // непосредственно инициализация переменной
        SCIP_CALL(SCIPaddVar(scip, var)); // добавляем эту переменную в модель
        start_vars[t.id - 1] = var; // чтобы обращаться к этой переменной по id задачи
    }

    // Создаем переменную makespan (общая длительность проекта (ЦФ))
    SCIP_VAR* makespan = nullptr;
    SCIP_CALL(SCIPcreateVarBasic(scip, &makespan, "makespan", 0.0, SCIPinfinity(scip), 1.0, SCIP_VARTYPE_CONTINUOUS));
    SCIP_CALL(SCIPaddVar(scip, makespan));

    // Ограничения предшествования
    for (const auto& t : inst.tasks) {
        for (int succ : t.successors) {  // succ -- id задачи-последователя
            SCIP_CONS* cons = nullptr;
            SCIP_VAR* vars[] = { start_vars[succ - 1], start_vars[t.id - 1] };
            SCIP_Real coefs[] = { 1.0, -1.0 };
            SCIP_CALL(SCIPcreateConsBasicLinear(scip, &cons, "prec", 2, vars, coefs, t.duration, SCIPinfinity(scip)));
            // теперь ограничение выглядит так:   1.0 * start_of_succ + (- 1.0) * start_of_t  \in  [t.duration, +inf]   ,   т.е. начало succ отсроит от начала t как минимум на t.duration
            SCIP_CALL(SCIPaddCons(scip, cons));  // добавляем это ограничение в модель
            SCIP_CALL(SCIPreleaseCons(scip, &cons));  // очищает оперативу от ограничения, т.к. оно уже добавлено в модель
        }
    }

    // Ограничения makespan >= start_i + duration_i    (логика аналогична предыдущему)
    // т.е. время окончания проекта не должно быть раньше чем время окончания каждой задачи
    for (const auto& t : inst.tasks) {
        SCIP_CONS* cons = nullptr;
        SCIP_VAR* vars[] = { makespan, start_vars[t.id - 1] };
        SCIP_Real coefs[] = { 1.0, -1.0 };
        SCIP_CALL(SCIPcreateConsBasicLinear(scip, &cons, "makespan", 2, vars, coefs, t.duration, SCIPinfinity(scip)));
        SCIP_CALL(SCIPaddCons(scip, cons));
        SCIP_CALL(SCIPreleaseCons(scip, &cons));
    }


    // сюда кастомные ограничения

    // Пример: неразрывность задач   n   и   n+1
    //                                 n+1 - 1             n - 1
    //                                                                                                               [n]                         [n]
    // {
    //     int first_task = 3;
    //     int second_task = 2;
    //     SCIP_CONS* cons = nullptr;
    //     SCIP_VAR* vars[] = { start_vars[second_task - 1], start_vars[first_task - 1] };
    //     SCIP_Real coefs[] = { 1.0, -1.0 };
    //     SCIP_CALL(SCIPcreateConsBasicLinear(scip, &cons, "contiguity_5_6", 2, vars, coefs, inst.tasks[first_task].duration, inst.tasks[first_task].duration));
    //     SCIP_CALL(SCIPaddCons(scip, cons));
    //     SCIP_CALL(SCIPreleaseCons(scip, &cons));
    //     std::cout << "custom precedence: " << first_task << " -> " << second_task << "\n";
    //     std::cout << "duration of task " << first_task << ": " << inst.tasks[first_task].duration << "\n\n";
    // }
    //
    // {
    //
    //     int first_task = 3;
    //     int second_task = 2;
    //     SCIP_CONS* cons = nullptr;
    //     SCIP_VAR* vars[] = { start_vars[second_task - 1], start_vars[first_task - 1] };
    //     SCIP_Real coefs[] = { 1.0, -1.0 };
    //     SCIP_CALL(SCIPcreateConsBasicLinear(scip, &cons, "contiguity_5_6", 2, vars, coefs, inst.tasks[first_task].duration, inst.tasks[first_task].duration));
    //     SCIP_CALL(SCIPaddCons(scip, cons));
    // }

    // std::cout << "inst.tasks[3].duration:  " << inst.tasks[3].duration << "\n";




    // ========  НАСТРОЙКА НЕДОСТУПНЫХ ИНТЕРВАЛОВ РЕСУРСОВ  ========
    // resource_id -> список интервалов [L, U)
    std::map<int, std::vector<std::pair<int,int>>> resource_unavailability;

    // Пример: здесь ЗАДАЁТЕ свои интервалы для каждого ресурса
    resource_unavailability[0] = { {1, 30}, {36, 40} };  // ресурс 0 недоступен в [10,20) и [50,60)
    resource_unavailability[1] = { {1, 30} };            // ресурс 1 недоступен в [30,40)
    resource_unavailability[2] = { {1, 30} };
    resource_unavailability[3] = { {1, 30} };


    // ========  ОГРАНИЧЕНИЯ НА РЕСУРСЫ ОТ ВРЕМЕНИ  ========
    
    // 1. Ограничения доступности ресурсов (resource capacity constraints)
    // Для каждого ресурса проверяем, что в каждый момент времени
    // суммарное потребление не превышает доступность
    
    // Создаем бинарные переменные для упорядочивания задач, которые конфликтуют по ресурсам
    // Для каждой пары задач (i, j), которые используют один и тот же ресурс r
    // и сумма их потребления превышает доступность ресурса, добавляем ограничение:
    // либо task_i заканчивается до начала task_j, либо наоборот
    
    for (int r = 0; r < inst.n_resources; ++r) {
        int capacity = inst.resources[r].capacity;
        
        // Находим все пары задач, которые конфликтуют по ресурсу r
        for (size_t i = 0; i < inst.tasks.size(); ++i) {
            const auto& task_i = inst.tasks[i];
            int usage_i = (r < (int)task_i.resources.size()) ? task_i.resources[r] : 0;
            if (usage_i == 0) continue; // задача не использует этот ресурс
            
            for (size_t j = i + 1; j < inst.tasks.size(); ++j) {
                const auto& task_j = inst.tasks[j];
                int usage_j = (r < (int)task_j.resources.size()) ? task_j.resources[r] : 0;
                if (usage_j == 0) continue; // задача не использует этот ресурс
                
                // Если суммарное потребление превышает доступность, задачи не могут выполняться одновременно
                if (usage_i + usage_j > capacity) {
                    // Создаем бинарную переменную для упорядочивания: y_ij = 1 если task_i идет до task_j
                    std::string y_name = "y_" + std::to_string(task_i.id) + "_" + std::to_string(task_j.id) + "_r" + std::to_string(r);
                    SCIP_VAR* y_var = nullptr;
                    SCIP_CALL(SCIPcreateVarBasic(scip, &y_var, y_name.c_str(), 0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY));
                    SCIP_CALL(SCIPaddVar(scip, y_var));
                    
                    // Большое число M для big-M ограничений (верхняя оценка makespan)
                    // Можно использовать сумму всех длительностей как оценку
                    SCIP_Real M = 0.0;
                    for (const auto& t : inst.tasks) {
                        M += t.duration;
                    }
                    M += 1000; // добавляем запас
                    
                    // Если y_ij = 1, то task_i заканчивается до начала task_j:
                    // start_j >= start_i + duration_i
                    SCIP_CONS* cons1 = nullptr;
                    SCIP_VAR* vars1[] = { start_vars[task_j.id - 1], start_vars[task_i.id - 1], y_var };
                    SCIP_Real coefs1[] = { 1.0, -1.0, -M };
                    SCIP_CALL(SCIPcreateConsBasicLinear(scip, &cons1, 
                        ("resource_order_" + std::to_string(task_i.id) + "_" + std::to_string(task_j.id) + "_r" + std::to_string(r) + "_1").c_str(),
                        3, vars1, coefs1, task_i.duration - M, SCIPinfinity(scip)));
                    SCIP_CALL(SCIPaddCons(scip, cons1));
                    SCIP_CALL(SCIPreleaseCons(scip, &cons1));
                    
                    // Если y_ij = 0, то task_j заканчивается до начала task_i:
                    // start_i >= start_j + duration_j
                    SCIP_CONS* cons2 = nullptr;
                    SCIP_VAR* vars2[] = { start_vars[task_i.id - 1], start_vars[task_j.id - 1], y_var };
                    SCIP_Real coefs2[] = { 1.0, -1.0, M };
                    SCIP_CALL(SCIPcreateConsBasicLinear(scip, &cons2,
                        ("resource_order_" + std::to_string(task_i.id) + "_" + std::to_string(task_j.id) + "_r" + std::to_string(r) + "_2").c_str(),
                        3, vars2, coefs2, task_j.duration, SCIPinfinity(scip)));
                    SCIP_CALL(SCIPaddCons(scip, cons2));
                    SCIP_CALL(SCIPreleaseCons(scip, &cons2));
                    
                    SCIP_CALL(SCIPreleaseVar(scip, &y_var));
                }
            }
        }
    }
    
    // 2. Ограничения на недоступные интервалы ресурсов
    // Для каждой задачи и каждого ресурса, который она использует,
    // проверяем, что задача не выполняется в недоступные интервалы
    for (const auto& task : inst.tasks) {
        for (int r = 0; r < inst.n_resources; ++r) {
            int usage = (r < (int)task.resources.size()) ? task.resources[r] : 0;
            if (usage == 0) continue; // задача не использует этот ресурс

            // Проверяем, есть ли недоступные интервалы для этого ресурса
            if (resource_unavailability.find(r) != resource_unavailability.end()) {
                for (const auto& [L, U] : resource_unavailability[r]) {
                    // Задача не может выполняться в интервале [L, U)
                    // Это значит: либо задача заканчивается до L, либо начинается после U

                    // Создаем бинарную переменную: z = 1 если задача заканчивается до L
                    std::string z_name = "z_" + std::to_string(task.id) + "_r" + std::to_string(r) + "_" +
                                        std::to_string(L) + "_" + std::to_string(U);
                    SCIP_VAR* z_var = nullptr;
                    SCIP_CALL(SCIPcreateVarBasic(scip, &z_var, z_name.c_str(), 0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY));
                    SCIP_CALL(SCIPaddVar(scip, z_var));

                    SCIP_Real M = 0.0;
                    for (const auto& t : inst.tasks) {
                        M += t.duration;
                    }
                    M += 1000;

                    // Если z = 1, то задача заканчивается до L: start + duration <= L
                    // start <= L - duration + M*(1-z)  =>  start + M*z <= L - duration + M
                    SCIP_CONS* cons_before = nullptr;
                    SCIP_VAR* vars_before[] = { start_vars[task.id - 1], z_var };
                    SCIP_Real coefs_before[] = { 1.0, M };
                    SCIP_CALL(SCIPcreateConsBasicLinear(scip, &cons_before,
                        ("unavail_before_" + std::to_string(task.id) + "_r" + std::to_string(r) + "_" +
                         std::to_string(L)).c_str(),
                        2, vars_before, coefs_before, -SCIPinfinity(scip), L - task.duration + M));
                    SCIP_CALL(SCIPaddCons(scip, cons_before));
                    SCIP_CALL(SCIPreleaseCons(scip, &cons_before));

                    // Если z = 0, то задача начинается после U: start >= U
                    // start >= U - M*z  =>  start + M*z >= U
                    SCIP_CONS* cons_after = nullptr;
                    SCIP_VAR* vars_after[] = { start_vars[task.id - 1], z_var };
                    SCIP_Real coefs_after[] = { 1.0, M };
                    SCIP_CALL(SCIPcreateConsBasicLinear(scip, &cons_after,
                        ("unavail_after_" + std::to_string(task.id) + "_r" + std::to_string(r) + "_" +
                         std::to_string(U)).c_str(),
                        2, vars_after, coefs_after, U, SCIPinfinity(scip)));
                    SCIP_CALL(SCIPaddCons(scip, cons_after));
                    SCIP_CALL(SCIPreleaseCons(scip, &cons_after));

                    SCIP_CALL(SCIPreleaseVar(scip, &z_var));
                }
            }
        }
    }





    // засечка времени
    auto t_start = std::chrono::high_resolution_clock::now();

    // Решаем
    SCIP_CALL(SCIPsolve(scip));   // непосредственно процесс решения (ветвления, LP, отсечения и тд)
    SCIP_SOL* sol = SCIPgetBestSol(scip);   // лучшее решение

    // еще одна засечка времени
    auto t_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = t_end - t_start;


    // Сохраняем пары (id, start_time)
    std::vector<std::pair<int, double>> starts;  // <-- объявлено здесь

    if (!sol) {
        std::cout << "No feasible solution found.\n";
    } else {
        std::cout << "\nOptimal makespan: " << SCIPgetSolVal(scip, sol, makespan) << "\n";

        // Сохраняем пары (id, start_time)
        // std::vector<std::pair<int, double>> starts;
        starts.reserve(inst.n_jobs);
        for (int id = 1; id <= inst.n_jobs; ++id) {
            double tstart = SCIPgetSolVal(scip, sol, start_vars[id - 1]);
            starts.emplace_back(id, tstart);
        }

        // вывод Task start times по порядку id
        std::cout << "\nTask start times:\n";
        for (auto& [id, time] : starts)
            std::cout << "t" << id << " = " << time << "\n";

        // Best order: сортировка по времени начала
        std::sort(starts.begin(), starts.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });

        std::cout << "\nBest order: ";
        for (auto& [id, _] : starts)
            std::cout << id << " ";
        std::cout << "\n";


        // Qt-приложение
        int argc = 0;
        char *argv[] = {nullptr};
        QApplication app(argc, argv);

        showGanttChart(starts, inst.tasks);

        return app.exec(); // откроет окно и держит его
    }





    // Освобождаем оперативу от переменных
    // В SCIP после SCIPaddVar переменная принадлежит модели,
    // но мы должны освободить наши локальные ссылки перед SCIPfree
    for (auto& v : start_vars) {
        if (v != nullptr) {
            SCIP_CALL(SCIPreleaseVar(scip, &v));
        }
    }
    if (makespan != nullptr) {
        SCIP_CALL(SCIPreleaseVar(scip, &makespan));
    }
    
    // Освобождаем SCIP (это автоматически освободит все переменные и ограничения)
    SCIP_CALL(SCIPfree(&scip));

    std::cout << "\nTime to solve: " << elapsed.count() << " seconds\n";

}