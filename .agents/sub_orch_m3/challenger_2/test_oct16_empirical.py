import math
import random

def clamp(val, min_v, max_v):
    return max(min_v, min(max_v, val))

def encode_normal_oct16(nx, ny, nz):
    l1 = abs(nx) + abs(ny) + abs(nz)
    if l1 < 0.0001:
        return 0x7F7F
    
    u = nx / l1
    v = ny / l1
    if nz < 0.0:
        u_old = u
        u = (1.0 - abs(v)) * (1.0 if u_old >= 0.0 else -1.0)
        v = (1.0 - abs(u_old)) * (1.0 if v >= 0.0 else -1.0)
        
    u_byte = clamp(int(round((u * 0.5 + 0.5) * 255.0)), 0, 255)
    v_byte = clamp(int(round((v * 0.5 + 0.5) * 255.0)), 0, 255)
    
    return u_byte | (v_byte << 8)

def decode_normal_oct16(oct16):
    u_byte = oct16 & 0xFF
    v_byte = (oct16 >> 8) & 0xFF
    
    u = (u_byte / 255.0) * 2.0 - 1.0
    v = (v_byte / 255.0) * 2.0 - 1.0
    
    z = 1.0 - abs(u) - abs(v)
    if z < 0.0:
        nx = (1.0 - abs(v)) * (1.0 if u >= 0.0 else -1.0)
        ny = (1.0 - abs(u)) * (1.0 if v >= 0.0 else -1.0)
        nz = z
    else:
        nx = u
        ny = v
        nz = z
        
    length = math.sqrt(nx*nx + ny*ny + nz*nz)
    if length > 0:
        nx /= length
        ny /= length
        nz /= length
    return nx, ny, nz

def run_test():
    random.seed(42)
    max_error_deg = 0.0
    total_error_deg = 0.0
    n_samples = 10000
    
    for _ in range(n_samples):
        # Uniform sampling on sphere
        z = random.uniform(-1.0, 1.0)
        theta = random.uniform(0, 2 * math.pi)
        r = math.sqrt(max(0.0, 1.0 - z*z))
        x = r * math.cos(theta)
        y = r * math.sin(theta)
        
        encoded = encode_normal_oct16(x, y, z)
        dx, dy, dz = decode_normal_oct16(encoded)
        
        dot = max(-1.0, min(1.0, x*dx + y*dy + z*dz))
        err_rad = math.acos(dot)
        err_deg = math.degrees(err_rad)
        
        if err_deg > max_error_deg:
            max_error_deg = err_deg
        total_error_deg += err_deg
        
    mean_error_deg = total_error_deg / n_samples
    print(f"Oct16 Precision Test over {n_samples} random unit vectors:")
    print(f"  Max Angular Error:  {max_error_deg:.4f} degrees ({math.radians(max_error_deg):.6f} rad)")
    print(f"  Mean Angular Error: {mean_error_deg:.4f} degrees ({math.radians(mean_error_deg):.6f} rad)")
    
if __name__ == "__main__":
    run_test()
