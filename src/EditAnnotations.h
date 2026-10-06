/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

struct EditAnnotationsWindow;
struct WindowTab;
struct Annotation;
struct MainWindow;

enum class EditAnnotFocus {
    Default,
    Edit,
    List,
};

void ShowEditAnnotationsWindow(WindowTab*, Annotation*, EditAnnotFocus focus = EditAnnotFocus::Default);
bool CloseAndDeleteEditAnnotationsWindow(WindowTab*);
void DeleteAnnotationAndUpdateUI(WindowTab*, Annotation*);
void SetSelectedAnnotation(WindowTab*, Annotation*, bool isNew = false, EditAnnotFocus focus = EditAnnotFocus::Default);
void UpdateAnnotationsList(EditAnnotationsWindow*);
void NotifyAnnotationsChanged(EditAnnotationsWindow*);

// smartpdf review mode (implemented in EditAnnotations.cpp)
void PublishReviewComments(MainWindow*, WindowTab*);
void ImportReviewComments(MainWindow*, WindowTab*);
void ClearImportedReviewComments(WindowTab*); // nullptr = all tabs
void ToggleReviewPanel(MainWindow*);
void RebuildReviewPanel(MainWindow*);
void RelayoutReviewPanel(MainWindow*);
void DestroyReviewPanel(MainWindow*);
