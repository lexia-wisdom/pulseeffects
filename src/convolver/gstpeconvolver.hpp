#ifndef GST_PECONVOLVER_HPP
#define GST_PECONVOLVER_HPP

#include <gst/audio/gstaudiofilter.h>
#include <zita-convolver.h>
#include <future>
#include <mutex>
#include <vector>

G_BEGIN_DECLS

#define GST_TYPE_PECONVOLVER (gst_peconvolver_get_type())
#define GST_PECONVOLVER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PECONVOLVER, GstPeconvolver))
#define GST_PECONVOLVER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_PECONVOLVER, GstPeconvolverClass))
#define GST_IS_PECONVOLVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_PECONVOLVER))
#define GST_IS_PECONVOLVER_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_PECONVOLVER))

struct GstPeconvolver {
  GstAudioFilter base_peconvolver;

  /* properties */

  gchar* kernel_path = nullptr;
  unsigned int ir_width, num_samples;

  /* < private > */

  bool ready;
  int rate, kernel_n_frames, irs_fail_count;
  int bpf;                      // bytes per frame : channels * bps
  std::vector<float> kernel_L;  // left channel buffer
  std::vector<float> kernel_R;  // right channel buffer

  std::string log_tag;

  Convproc* conv = nullptr;

  std::mutex lock_guard_zita;

  std::vector<std::future<void>> futures;
};

struct GstPeconvolverClass {
  GstAudioFilterClass base_peconvolver_class;
};

GType gst_peconvolver_get_type(void);

G_END_DECLS

#endif
